/*
 * Delphine Software International CIN Audio/Video Decoders
 * Copyright (c) 2006 Gregory Montoir (cyx@users.sourceforge.net)
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
 * Delphine Software International CIN audio/video decoders
 */

#include "libavutil/channel_layout.h"
#include "avcodec.h"
#include "bytestream.h"
#include "internal.h"
#include "mathops.h"


typedef enum CinVideoBitmapIndex {
    CIN_CUR_BMP = 0, /* current */
    CIN_PRE_BMP = 1, /* previous */
    CIN_INT_BMP = 2  /* intermediate */
} CinVideoBitmapIndex;

typedef struct CinVideoContext {
    AVCodecContext *avctx;
    AVFrame frame;
    unsigned int bitmap_size;
    uint32_t palette[256];
    uint8_t *bitmap_table[3];
} CinVideoContext;

typedef struct CinAudioContext {
    int initial_decode_frame;
    int delta;
} CinAudioContext;


/* table defining a geometric sequence with multiplier = 32767 ^ (1 / 128) */
static const int16_t cinaudio_delta16_table[256] = {
         0,      0,      0,      0,      0,      0,      0,      0,
         0,      0,      0,      0,      0,      0,      0,      0,
         0,      0,      0, -30210, -27853, -25680, -23677, -21829,
    -20126, -18556, -17108, -15774, -14543, -13408, -12362, -11398,
    -10508,  -9689,  -8933,  -8236,  -7593,  -7001,  -6455,  -5951,
     -5487,  -5059,  -4664,  -4300,  -3964,  -3655,  -3370,  -3107,
     -2865,  -2641,  -2435,  -2245,  -2070,  -1908,  -1759,  -1622,
     -1495,  -1379,  -1271,  -1172,  -1080,   -996,   -918,   -847,
      -781,   -720,   -663,   -612,   -564,   -520,   -479,   -442,
      -407,   -376,   -346,   -319,   -294,   -271,   -250,   -230,
      -212,   -196,   -181,   -166,   -153,   -141,   -130,   -120,
      -111,   -102,    -94,    -87,    -80,    -74,    -68,    -62,
       -58,    -53,    -49,    -45,    -41,    -38,    -35,    -32,
       -30,    -27,    -25,    -23,    -21,    -20,    -18,    -17,
       -15,    -14,    -13,    -12,    -11,    -10,     -9,     -8,
        -7,     -6,     -5,     -4,     -3,     -2,     -1,      0,
         0,      1,      2,      3,      4,      5,      6,      7,
         8,      9,     10,     11,     12,     13,     14,     15,
        17,     18,     20,     21,     23,     25,     27,     30,
        32,     35,     38,     41,     45,     49,     53,     58,
        62,     68,     74,     80,     87,     94,    102,    111,
       120,    130,    141,    153,    166,    181,    196,    212,
       230,    250,    271,    294,    319,    346,    376,    407,
       442,    479,    520,    564,    612,    663,    720,    781,
       847,    918,    996,   1080,   1172,   1271,   1379,   1495,
      1622,   1759,   1908,   2070,   2245,   2435,   2641,   2865,
      3107,   3370,   3655,   3964,   4300,   4664,   5059,   5487,
      5951,   6455,   7001,   7593,   8236,   8933,   9689,  10508,
     11398,  12362,  13408,  14543,  15774,  17108,  18556,  20126,
     21829,  23677,  25680,  27853,  30210,      0,      0,      0,
         0,      0,      0,      0,      0,      0,      0,      0,
         0,      0,      0,      0,      0,      0,      0,      0
};

static av_cold void destroy_buffers(CinVideoContext *cin)
{
    int i;

    for (i = 0; i < 3; ++i)
        av_freep(&cin->bitmap_table[i]);
}

static av_cold int allocate_buffers(CinVideoContext *cin)
{
    int i;

    for (i = 0; i < 3; ++i) {
        cin->bitmap_table[i] = av_mallocz(cin->bitmap_size);
        if (!cin->bitmap_table[i]) {
            av_log(cin->avctx, AV_LOG_ERROR, "Can't allocate bitmap buffers.\n");
            destroy_buffers(cin);
            return AVERROR(ENOMEM);
        }
    }

    return 0;
}

static av_cold int cinvideo_decode_init(AVCodecContext *avctx)
{
    CinVideoContext *cin = avctx->priv_data;

    cin->avctx = avctx;
    avctx->pix_fmt = AV_PIX_FMT_PAL8;

    avcodec_get_frame_defaults(&cin->frame);

    cin->bitmap_size = avctx->width * avctx->height;
    if (allocate_buffers(cin))
        return AVERROR(ENOMEM);

    return 0;
}

static void cin_apply_delta_data(const unsigned char *src, unsigned char *dst,
                                 int size)
{
    while (size--)
        *dst++ += *src++;
}

static int cin_decode_huffman(const unsigned char *src, int src_size,
                              unsigned char *dst, int dst_size)
{
    int b, huff_code = 0;
    unsigned char huff_code_table[15];
    unsigned char *dst_cur       = dst;
    unsigned char *dst_end       = dst + dst_size;
    const unsigned char *src_end = src + src_size;

    memcpy(huff_code_table, src, 15);
    src += 15;

    while (src < src_end) {
        huff_code = *src++;
        if ((huff_code >> 4) == 15) {
            b          = huff_code << 4;
            huff_code  = *src++;
            *dst_cur++ = b | (huff_code >> 4);
        } else
            *dst_cur++ = huff_code_table[huff_code >> 4];
        if (dst_cur >= dst_end)
            break;

        huff_code &= 15;
        if (huff_code == 15) {
            *dst_cur++ = *src++;
        } else
            *dst_cur++ = huff_code_table[huff_code];
        if (dst_cur >= dst_end)
            break;
    }

    return dst_cur - dst;
}

static int cin_decode_lzss(const unsigned char *src, int src_size,
                           unsigned char *dst, int dst_size)
{
    uint16_t cmd;
    int i, sz, offset, code;
    unsigned char *dst_end       = dst + dst_size, *dst_start = dst;
    const unsigned char *src_end = src + src_size;

    while (src < src_end && dst < dst_end) {
        code = *src++;
        for (i = 0; i < 8 && src < src_end && dst < dst_end; ++i) {
            if (code & (1 << i)) {
                *dst++ = *src++;
            } else {
                cmd    = AV_RL16(src);
                src   += 2;
                offset = cmd >> 4;
                if ((int)(dst - dst_start) < offset + 1)
                    return AVERROR_INVALIDDATA;
                sz = (cmd & 0xF) + 2;
                /* don't use memcpy/memmove here as the decoding routine
                 * (ab)uses buffer overlappings to repeat bytes in the
                 * destination */
                sz = FFMIN(sz, dst_end - dst);
                while (sz--) {
                    *dst = *(dst - offset - 1);
                    ++dst;
                }
            }
        }
    }

    return 0;
}

static int cin_decode_rle(const unsigned char *src, int src_size,
                           unsigned char *dst, int dst_size)
{
    int len, code;
    unsigned char *dst_end       = dst + dst_size;
    const unsigned char *src_end = src + src_size;

    while (src + 1 < src_end && dst < dst_end) {
        code = *src++;
        if (code & 0x80) {
            len = code - 0x7F;
            memset(dst, *src++, FFMIN(len, dst_end - dst));
        } else {
            len = code + 1;
            if (len > src_end-src) {
                av_log(NULL, AV_LOG_ERROR, "RLE overread\n");
                return AVERROR_INVALIDDATA;
            }
            memcpy(dst, src, FFMIN3(len, dst_end - dst, src_end - src));
            src += len;
        }
        dst += len;
    }
    return 0;
}

static int cinvideo_decode_frame(AVCodecContext *avctx,
                                 void *data, int *got_frame,
                                 AVPacket *avpkt)
{
    const uint8_t *buf   = avpkt->data;
    int buf_size         = avpkt->size;
    CinVideoContext *cin = avctx->priv_data;
    int i, y, palette_type, palette_colors_count,
        bitmap_frame_type, bitmap_frame_size, res = 0;

    palette_type         = buf[0];
    palette_colors_count = AV_RL16(buf + 1);
    bitmap_frame_type    = buf[3];
    buf                 += 4;

    bitmap_frame_size = buf_size - 4;

    /* handle palette */
    if (bitmap_frame_size < palette_colors_count * (3 + (palette_type != 0)))
        return AVERROR_INVALIDDATA;
    if (palette_type == 0) {
        if (palette_colors_count > 256)
            return AVERROR_INVALIDDATA;
        for (i = 0; i < palette_colors_count; ++i) {
            cin->palette[i]    = 0xFFU << 24 | bytestream_get_le24(&buf);
            bitmap_frame_size -= 3;
        }
    } else {
        for (i = 0; i < palette_colors_count; ++i) {
            cin->palette[buf[0]] = 0xFFU << 24 | AV_RL24(buf + 1);
            buf                 += 4;
            bitmap_frame_size   -= 4;
        }
    }

    /* note: the decoding routines below assumes that
     * surface.width = surface.pitch */
    switch (bitmap_frame_type) {
    case 9:
        cin_decode_rle(buf, bitmap_frame_size,
                       cin->bitmap_table[CIN_CUR_BMP], cin->bitmap_size);
        break;
    case 34:
        cin_decode_rle(buf, bitmap_frame_size,
                       cin->bitmap_table[CIN_CUR_BMP], cin->bitmap_size);
        cin_apply_delta_data(cin->bitmap_table[CIN_PRE_BMP],
                             cin->bitmap_table[CIN_CUR_BMP], cin->bitmap_size);
        break;
    case 35:
        bitmap_frame_size = cin_decode_huffman(buf, bitmap_frame_size,
                           cin->bitmap_table[CIN_INT_BMP], cin->bitmap_size);
        cin_decode_rle(cin->bitmap_table[CIN_INT_BMP], bitmap_frame_size,
                       cin->bitmap_table[CIN_CUR_BMP], cin->bitmap_size);
        break;
    case 36:
        bitmap_frame_size = cin_decode_huffman(buf, bitmap_frame_size,
                                               cin->bitmap_table[CIN_INT_BMP],
                                               cin->bitmap_size);
        cin_decode_rle(cin->bitmap_table[CIN_INT_BMP], bitmap_frame_size,
                       cin->bitmap_table[CIN_CUR_BMP], cin->bitmap_size);
        cin_apply_delta_data(cin->bitmap_table[CIN_PRE_BMP],
                             cin->bitmap_table[CIN_CUR_BMP], cin->bitmap_size);
        break;
    case 37:
        cin_decode_huffman(buf, bitmap_frame_size,
                           cin->bitmap_table[CIN_CUR_BMP], cin->bitmap_size);
        break;
    case 38:
        res = cin_decode_lzss(buf, bitmap_frame_size,
                              cin->bitmap_table[CIN_CUR_BMP],
                              cin->bitmap_size);
        if (res < 0)
            return res;
        break;
    case 39:
        res = cin_decode_lzss(buf, bitmap_frame_size,
                              cin->bitmap_table[CIN_CUR_BMP],
                              cin->bitmap_size);
        if (res < 0)
            return res;
        cin_apply_delta_data(cin->bitmap_table[CIN_PRE_BMP],
                             cin->bitmap_table[CIN_CUR_BMP], cin->bitmap_size);
        break;
    }

    if ((res = ff_reget_buffer(avctx, &cin->frame)) < 0)
        return res;

    memcpy(cin->frame.data[1], cin->palette, sizeof(cin->palette));
    cin->frame.palette_has_changed = 1;
    for (y = 0; y < cin->avctx->height; ++y)
        memcpy(cin->frame.data[0] + (cin->avctx->height - 1 - y) * cin->frame.linesize[0],
               cin->bitmap_table[CIN_CUR_BMP] + y * cin->avctx->width,
               cin->avctx->width);

    FFSWAP(uint8_t *, cin->bitmap_table[CIN_CUR_BMP],
                      cin->bitmap_table[CIN_PRE_BMP]);

    if ((res = av_frame_ref(data, &cin->frame)) < 0)
        return res;

    *got_frame = 1;

    return buf_size;
}

static av_cold int cinvideo_decode_end(AVCodecContext *avctx)
{
    CinVideoContext *cin = avctx->priv_data;

    av_frame_unref(&cin->frame);

    destroy_buffers(cin);

    return 0;
}

static av_cold int cinaudio_decode_init(AVCodecContext *avctx)
{
    CinAudioContext *cin = avctx->priv_data;

    cin->initial_decode_frame = 1;
    cin->delta                = 0;
    avctx->sample_fmt         = AV_SAMPLE_FMT_S16;
    avctx->channels           = 1;
    avctx->channel_layout     = AV_CH_LAYOUT_MONO;

    return 0;
}

static int cinaudio_decode_frame(AVCodecContext *avctx, void *data,
                                 int *got_frame_ptr, AVPacket *avpkt)
{
    AVFrame *frame         = data;
    const uint8_t *buf     = avpkt->data;
    CinAudioContext *cin   = avctx->priv_data;
    const uint8_t *buf_end = buf + avpkt->size;
    int16_t *samples;
    int delta, ret;

    /* get output buffer */
    frame->nb_samples = avpkt->size - cin->initial_decode_frame;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;
    samples = (int16_t *)frame->data[0];

    delta = cin->delta;
    if (cin->initial_decode_frame) {
        cin->initial_decode_frame = 0;
        delta                     = sign_extend(AV_RL16(buf), 16);
        buf                      += 2;
        *samples++                = delta;
    }
    while (buf < buf_end) {
        delta     += cinaudio_delta16_table[*buf++];
        delta      = av_clip_int16(delta);
        *samples++ = delta;
    }
    cin->delta = delta;

    *got_frame_ptr = 1;

    return avpkt->size;
}

AVCodec ff_dsicinvideo_decoder = {
    .name           = "dsicinvideo",
    .long_name      = NULL_IF_CONFIG_SMALL("Delphine Software International CIN video"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_DSICINVIDEO,
    .priv_data_size = sizeof(CinVideoContext),
    .init           = cinvideo_decode_init,
    .close          = cinvideo_decode_end,
    .decode         = cinvideo_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
};

AVCodec ff_dsicinaudio_decoder = {
    .name           = "dsicinaudio",
    .long_name      = NULL_IF_CONFIG_SMALL("Delphine Software International CIN audio"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_DSICINAUDIO,
    .priv_data_size = sizeof(CinAudioContext),
    .init           = cinaudio_decode_init,
    .decode         = cinaudio_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
};
