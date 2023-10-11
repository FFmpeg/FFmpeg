/*
 * Copyright (c) 2020 Paul B Mahol
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
 * Cineform HD video encoder
 */

#include <stddef.h>

#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "avcodec.h"
#include "bytestream.h"
#include "cfhd.h"
#include "cfhdencdsp.h"
#include "codec_internal.h"
#include "encode.h"
#include "put_bits.h"

/* Derived from existing tables from decoder */
static const unsigned codebook[256][2] = {
    { 1, 0x00000000 }, { 2, 0x00000002 }, { 3, 0x00000007 }, { 5, 0x00000019 }, { 6, 0x00000030 },
    { 6, 0x00000036 }, { 7, 0x00000063 }, { 7, 0x0000006B }, { 7, 0x0000006F }, { 8, 0x000000D4 },
    { 8, 0x000000DC }, { 9, 0x00000189 }, { 9, 0x000001A0 }, { 9, 0x000001AB }, {10, 0x00000310 },
    {10, 0x00000316 }, {10, 0x00000354 }, {10, 0x00000375 }, {10, 0x00000377 }, {11, 0x00000623 },
    {11, 0x00000684 }, {11, 0x000006AB }, {11, 0x000006EC }, {12, 0x00000C44 }, {12, 0x00000C5C },
    {12, 0x00000C5E }, {12, 0x00000D55 }, {12, 0x00000DD1 }, {12, 0x00000DD3 }, {12, 0x00000DDB },
    {13, 0x0000188B }, {13, 0x000018BB }, {13, 0x00001AA8 }, {13, 0x00001BA0 }, {13, 0x00001BA4 },
    {13, 0x00001BB5 }, {14, 0x00003115 }, {14, 0x00003175 }, {14, 0x0000317D }, {14, 0x00003553 },
    {14, 0x00003768 }, {15, 0x00006228 }, {15, 0x000062E8 }, {15, 0x000062F8 }, {15, 0x00006AA4 },
    {15, 0x00006E85 }, {15, 0x00006E87 }, {15, 0x00006ED3 }, {16, 0x0000C453 }, {16, 0x0000C5D3 },
    {16, 0x0000C5F3 }, {16, 0x0000DD08 }, {16, 0x0000DD0C }, {16, 0x0000DDA4 }, {17, 0x000188A4 },
    {17, 0x00018BA5 }, {17, 0x00018BE5 }, {17, 0x0001AA95 }, {17, 0x0001AA97 }, {17, 0x0001BA13 },
    {17, 0x0001BB4A }, {17, 0x0001BB4B }, {18, 0x00031748 }, {18, 0x000317C8 }, {18, 0x00035528 },
    {18, 0x0003552C }, {18, 0x00037424 }, {18, 0x00037434 }, {18, 0x00037436 }, {19, 0x00062294 },
    {19, 0x00062E92 }, {19, 0x00062F92 }, {19, 0x0006AA52 }, {19, 0x0006AA5A }, {19, 0x0006E84A },
    {19, 0x0006E86A }, {19, 0x0006E86E }, {20, 0x000C452A }, {20, 0x000C5D27 }, {20, 0x000C5F26 },
    {20, 0x000D54A6 }, {20, 0x000D54B6 }, {20, 0x000DD096 }, {20, 0x000DD0D6 }, {20, 0x000DD0DE },
    {21, 0x00188A56 }, {21, 0x0018BA4D }, {21, 0x0018BE4E }, {21, 0x0018BE4F }, {21, 0x001AA96E },
    {21, 0x001BA12E }, {21, 0x001BA12F }, {21, 0x001BA1AF }, {21, 0x001BA1BF }, {22, 0x00317498 },
    {22, 0x0035529C }, {22, 0x0035529D }, {22, 0x003552DE }, {22, 0x003552DF }, {22, 0x0037435D },
    {22, 0x0037437D }, {23, 0x0062295D }, {23, 0x0062E933 }, {23, 0x006AA53D }, {23, 0x006AA53E },
    {23, 0x006AA53F }, {23, 0x006E86B9 }, {23, 0x006E86F8 }, {24, 0x00C452B8 }, {24, 0x00C5D265 },
    {24, 0x00D54A78 }, {24, 0x00D54A79 }, {24, 0x00DD0D70 }, {24, 0x00DD0D71 }, {24, 0x00DD0DF2 },
    {24, 0x00DD0DF3 }, {26, 0x03114BA2 }, {25, 0x0188A5B1 }, {25, 0x0188A58B }, {25, 0x0188A595 },
    {25, 0x0188A5D6 }, {25, 0x0188A5D7 }, {25, 0x0188A5A8 }, {25, 0x0188A5AE }, {25, 0x0188A5AF },
    {25, 0x0188A5C4 }, {25, 0x0188A5C5 }, {25, 0x0188A587 }, {25, 0x0188A584 }, {25, 0x0188A585 },
    {25, 0x0188A5C6 }, {25, 0x0188A5C7 }, {25, 0x0188A5CC }, {25, 0x0188A5CD }, {25, 0x0188A581 },
    {25, 0x0188A582 }, {25, 0x0188A583 }, {25, 0x0188A5CE }, {25, 0x0188A5CF }, {25, 0x0188A5C2 },
    {25, 0x0188A5C3 }, {25, 0x0188A5C1 }, {25, 0x0188A5B4 }, {25, 0x0188A5B5 }, {25, 0x0188A5E6 },
    {25, 0x0188A5E7 }, {25, 0x0188A5E4 }, {25, 0x0188A5E5 }, {25, 0x0188A5AB }, {25, 0x0188A5E0 },
    {25, 0x0188A5E1 }, {25, 0x0188A5E2 }, {25, 0x0188A5E3 }, {25, 0x0188A5B6 }, {25, 0x0188A5B7 },
    {25, 0x0188A5FD }, {25, 0x0188A57E }, {25, 0x0188A57F }, {25, 0x0188A5EC }, {25, 0x0188A5ED },
    {25, 0x0188A5FE }, {25, 0x0188A5FF }, {25, 0x0188A57D }, {25, 0x0188A59C }, {25, 0x0188A59D },
    {25, 0x0188A5E8 }, {25, 0x0188A5E9 }, {25, 0x0188A5EA }, {25, 0x0188A5EB }, {25, 0x0188A5EF },
    {25, 0x0188A57A }, {25, 0x0188A57B }, {25, 0x0188A578 }, {25, 0x0188A579 }, {25, 0x0188A5BA },
    {25, 0x0188A5BB }, {25, 0x0188A5B8 }, {25, 0x0188A5B9 }, {25, 0x0188A588 }, {25, 0x0188A589 },
    {25, 0x018BA4C8 }, {25, 0x018BA4C9 }, {25, 0x0188A5FA }, {25, 0x0188A5FB }, {25, 0x0188A5BC },
    {25, 0x0188A5BD }, {25, 0x0188A598 }, {25, 0x0188A599 }, {25, 0x0188A5F4 }, {25, 0x0188A5F5 },
    {25, 0x0188A59B }, {25, 0x0188A5DE }, {25, 0x0188A5DF }, {25, 0x0188A596 }, {25, 0x0188A597 },
    {25, 0x0188A5F8 }, {25, 0x0188A5F9 }, {25, 0x0188A5F1 }, {25, 0x0188A58E }, {25, 0x0188A58F },
    {25, 0x0188A5DC }, {25, 0x0188A5DD }, {25, 0x0188A5F2 }, {25, 0x0188A5F3 }, {25, 0x0188A58C },
    {25, 0x0188A58D }, {25, 0x0188A5A4 }, {25, 0x0188A5F0 }, {25, 0x0188A5A5 }, {25, 0x0188A5A6 },
    {25, 0x0188A5A7 }, {25, 0x0188A59A }, {25, 0x0188A5A2 }, {25, 0x0188A5A3 }, {25, 0x0188A58A },
    {25, 0x0188A5B0 }, {25, 0x0188A5A0 }, {25, 0x0188A5A1 }, {25, 0x0188A5DA }, {25, 0x0188A5DB },
    {25, 0x0188A59E }, {25, 0x0188A59F }, {25, 0x0188A5D8 }, {25, 0x0188A5EE }, {25, 0x0188A5D9 },
    {25, 0x0188A5F6 }, {25, 0x0188A5F7 }, {25, 0x0188A57C }, {25, 0x0188A5C8 }, {25, 0x0188A5C9 },
    {25, 0x0188A594 }, {25, 0x0188A5FC }, {25, 0x0188A5CA }, {25, 0x0188A5CB }, {25, 0x0188A5B2 },
    {25, 0x0188A5AA }, {25, 0x0188A5B3 }, {25, 0x0188A572 }, {25, 0x0188A573 }, {25, 0x0188A5C0 },
    {25, 0x0188A5BE }, {25, 0x0188A5BF }, {25, 0x0188A592 }, {25, 0x0188A580 }, {25, 0x0188A593 },
    {25, 0x0188A590 }, {25, 0x0188A591 }, {25, 0x0188A586 }, {25, 0x0188A5A9 }, {25, 0x0188A5D2 },
    {25, 0x0188A5D3 }, {25, 0x0188A5D4 }, {25, 0x0188A5D5 }, {25, 0x0188A5AC }, {25, 0x0188A5AD },
    {25, 0x0188A5D0 },
};

/* Derived by extracting runcodes from existing tables from decoder */
static const uint16_t runbook[18][3] = {
    {1,  0x0000,   1}, {2,  0x0000,   2}, {3,  0x0000,   3}, {4,  0x0000,   4},
    {5,  0x0000,   5}, {6,  0x0000,   6}, {7,  0x0000,   7}, {8,  0x0000,   8},
    {9,  0x0000,   9}, {10, 0x0000,  10}, {11, 0x0000,  11},
    {7,  0x0069,  12}, {8,  0x00D1,  20}, {9,  0x018A,  32},
    {10, 0x0343,  60}, {11, 0x0685, 100}, {13, 0x18BF, 180}, {13, 0x1BA5, 320},
};

/*
 * Derived by inspecting various quality encodes
 * and adding some more from scratch.
 */
static const uint16_t quantization_per_subband[2][3][13][9] = {
    {{
        { 16, 16,  8,  4,  4,  2,   6,   6,   9, }, // film3+
        { 16, 16,  8,  4,  4,  2,   6,   6,   9, }, // film3
        { 16, 16,  8,  4,  4,  2,   7,   7,  10, }, // film2+
        { 16, 16,  8,  4,  4,  2,   8,   8,  12, }, // film2
        { 16, 16,  8,  4,  4,  2,  16,  16,  26, }, // film1++
        { 24, 24, 12,  6,  6,  3,  24,  24,  36, }, // film1+
        { 24, 24, 12,  6,  6,  3,  24,  24,  36, }, // film1
        { 32, 32, 24,  8,  8,  6,  32,  32,  48, }, // high+
        { 32, 32, 24,  8,  8,  6,  32,  32,  48, }, // high
        { 48, 48, 32, 12, 12,  8,  64,  64,  96, }, // medium+
        { 48, 48, 32, 12, 12,  8,  64,  64,  96, }, // medium
        { 64, 64, 48, 16, 16, 12,  96,  96, 144, }, // low+
        { 64, 64, 48, 16, 16, 12, 128, 128, 192, }, // low
    },
    {
        { 16, 16,  8,  4,  4,  2,   6,   6,   9, }, // film3+
        { 16, 16,  8,  4,  4,  2,   6,   6,  12, }, // film3
        { 16, 16,  8,  4,  4,  2,   7,   7,  14, }, // film2+
        { 16, 16,  8,  4,  4,  2,   8,   8,  16, }, // film2
        { 16, 16,  8,  4,  4,  2,  16,  16,  26, }, // film1++
        { 24, 24, 12,  6,  6,  3,  24,  24,  36, }, // film1+
        { 24, 24, 12,  6,  6,  3,  24,  24,  48, }, // film1
        { 32, 32, 24,  8,  8,  6,  32,  32,  48, }, // high+
        { 48, 48, 32, 12, 12,  8,  32,  32,  64, }, // high
        { 48, 48, 32, 12, 12,  8,  64,  64,  96, }, // medium+
        { 48, 48, 32, 12, 12,  8,  64,  64, 128, }, // medium
        { 64, 64, 48, 16, 16, 12,  96,  96, 160, }, // low+
        { 64, 64, 48, 16, 16, 12, 128, 128, 192, }, // low
    },
    {
        { 16, 16,  8,  4,  4,  2,   6,   6,   9, }, // film3+
        { 16, 16,  8,  4,  4,  2,   6,   6,  12, }, // film3
        { 16, 16,  8,  4,  4,  2,   7,   7,  14, }, // film2+
        { 16, 16,  8,  4,  4,  2,   8,   8,  16, }, // film2
        { 16, 16,  8,  4,  4,  2,  16,  16,  26, }, // film1++
        { 24, 24, 12,  6,  6,  3,  24,  24,  36, }, // film1+
        { 24, 24, 12,  6,  6,  3,  24,  24,  48, }, // film1
        { 32, 32, 24,  8,  8,  6,  32,  32,  48, }, // high+
        { 48, 48, 32, 12, 12,  8,  32,  32,  64, }, // high
        { 48, 48, 32, 12, 12,  8,  64,  64,  96, }, // medium+
        { 48, 48, 32, 12, 12,  8,  64,  64, 128, }, // medium
        { 64, 64, 48, 16, 16, 12,  96,  96, 160, }, // low+
        { 64, 64, 48, 16, 16, 12, 128, 128, 192, }, // low
    }},
    {{
        { 16, 16,  8, 16, 16,  8,  24,  24,  36, }, // film3+
        { 16, 16,  8, 16, 16,  8,  24,  24,  36, }, // film3
        { 16, 16,  8, 16, 16,  8,  32,  32,  48, }, // film2+
        { 16, 16,  8, 16, 16,  8,  32,  32,  48, }, // film2
        { 16, 16,  8, 20, 20, 10,  80,  80, 128, }, // film1++
        { 24, 24, 12, 24, 24, 12,  96,  96, 144, }, // film1+
        { 24, 24, 12, 24, 24, 12,  96,  96, 144, }, // film1
        { 32, 32, 24, 32, 32, 24, 128, 128, 192, }, // high+
        { 32, 32, 24, 32, 32, 24, 128, 128, 192, }, // high
        { 48, 48, 32, 48, 48, 32, 256, 256, 384, }, // medium+
        { 48, 48, 32, 48, 48, 32, 256, 256, 384, }, // medium
        { 56, 56, 40, 56, 56, 40, 512, 512, 768, }, // low+
        { 64, 64, 48, 64, 64, 48, 512, 512, 768, }, // low
    },
    {
        { 16, 16,  8, 16, 16,  8,  24,  24,  36, }, // film3+
        { 16, 16,  8, 16, 16,  8,  48,  48,  72, }, // film3
        { 16, 16,  8, 16, 16,  8,  48,  48,  72, }, // film2+
        { 16, 16,  8, 16, 16,  8,  64,  64,  96, }, // film2
        { 16, 16,  8, 20, 20, 10,  80,  80, 128, }, // film1++
        { 24, 24, 12, 24, 24, 12,  96,  96, 144, }, // film1+
        { 24, 24, 12, 24, 24, 12, 192, 192, 288, }, // film1
        { 32, 32, 24, 32, 32, 24, 128, 128, 192, }, // high+
        { 32, 32, 24, 32, 32, 24, 256, 256, 384, }, // high
        { 48, 48, 32, 48, 48, 32, 256, 256, 384, }, // medium+
        { 48, 48, 32, 48, 48, 32, 512, 512, 768, }, // medium
        { 56, 56, 40, 56, 56, 40, 512, 512, 768, }, // low+
        { 64, 64, 48, 64, 64, 48,1024,1024,1536, }, // low
    },
    {
        { 16, 16,  8, 16, 16,  8,  24,  24,  36, }, // film3+
        { 16, 16,  8, 16, 16,  8,  48,  48,  72, }, // film3
        { 16, 16,  8, 16, 16,  8,  48,  48,  72, }, // film2+
        { 16, 16,  8, 16, 16,  8,  64,  64,  96, }, // film2
        { 16, 16, 10, 20, 20, 10,  80,  80, 128, }, // film1++
        { 24, 24, 12, 24, 24, 12,  96,  96, 144, }, // film1+
        { 24, 24, 12, 24, 24, 12, 192, 192, 288, }, // film1
        { 32, 32, 24, 32, 32, 24, 128, 128, 192, }, // high+
        { 32, 32, 24, 32, 32, 24, 256, 256, 384, }, // high
        { 48, 48, 32, 48, 48, 32, 256, 256, 384, }, // medium+
        { 48, 48, 32, 48, 48, 32, 512, 512, 768, }, // medium
        { 56, 56, 40, 56, 56, 40, 512, 512, 768, }, // low+
        { 64, 64, 48, 64, 64, 48,1024,1024,1536, }, // low
    }},
};

typedef struct Codebook {
    unsigned bits;
    unsigned size;
} Codebook;

typedef struct Runbook {
    unsigned size;
    unsigned bits;
    unsigned run;
} Runbook;

typedef struct PlaneEnc {
    unsigned size;

    int16_t *dwt_buf;
    int16_t *dwt_tmp;

    unsigned quantization[SUBBAND_COUNT];
    int16_t *subband[SUBBAND_COUNT];
    int16_t *l_h[8];

    SubBand band[DWT_LEVELS][4];
} PlaneEnc;

typedef struct CFHDEncContext {
    const AVClass *class;

    PutBitContext       pb;
    PutByteContext      pby;

    int quality;
    int planes;
    int chroma_h_shift;
    int chroma_v_shift;
    PlaneEnc plane[4];

    uint16_t lut[1024];
    Runbook  rb[321];
    Codebook cb[513];
    int16_t *alpha;

    CFHDEncDSPContext dsp;
} CFHDEncContext;

static av_cold int cfhd_encode_init(AVCodecContext *avctx)
{
    CFHDEncContext *s = avctx->priv_data;
    const int sign_mask = 256;
    const int twos_complement = -sign_mask;
    const int mag_mask = sign_mask - 1;
    int ret, last = 0;

    ret = av_pix_fmt_get_chroma_sub_sample(avctx->pix_fmt,
                                           &s->chroma_h_shift,
                                           &s->chroma_v_shift);
    if (ret < 0)
        return ret;

    if (avctx->height < 32) {
        av_log(avctx, AV_LOG_ERROR, "Height must be >= 32.\n");
        return AVERROR_INVALIDDATA;
    }

    if (avctx->width & 15) {
        av_log(avctx, AV_LOG_ERROR, "Width must be multiple of 16.\n");
        return AVERROR_INVALIDDATA;
    }

    s->planes = av_pix_fmt_count_planes(avctx->pix_fmt);

    for (int i = 0; i < s->planes; i++) {
        int w8, h8, w4, h4, w2, h2;
        const int a_height = FFALIGN(avctx->height, 8);
        int width  = i ? AV_CEIL_RSHIFT(avctx->width, s->chroma_h_shift) : avctx->width;
        int height = i ? a_height >> s->chroma_v_shift: a_height;

        w8 = width / 8 + 64;
        h8 = height / 8;
        w4 = w8 * 2;
        h4 = h8 * 2;
        w2 = w4 * 2;
        h2 = h4 * 2;

        s->plane[i].dwt_buf =
            av_calloc(h8 * 8 * w8 * 8, sizeof(*s->plane[i].dwt_buf));
        s->plane[i].dwt_tmp =
            av_malloc_array(h8 * 8 * w8 * 8, sizeof(*s->plane[i].dwt_tmp));
        if (!s->plane[i].dwt_buf || !s->plane[i].dwt_tmp)
            return AVERROR(ENOMEM);

        s->plane[i].subband[0] = s->plane[i].dwt_buf;
        s->plane[i].subband[1] = s->plane[i].dwt_buf + 2 * w8 * h8;
        s->plane[i].subband[2] = s->plane[i].dwt_buf + 1 * w8 * h8;
        s->plane[i].subband[3] = s->plane[i].dwt_buf + 3 * w8 * h8;
        s->plane[i].subband[4] = s->plane[i].dwt_buf + 2 * w4 * h4;
        s->plane[i].subband[5] = s->plane[i].dwt_buf + 1 * w4 * h4;
        s->plane[i].subband[6] = s->plane[i].dwt_buf + 3 * w4 * h4;
        s->plane[i].subband[7] = s->plane[i].dwt_buf + 2 * w2 * h2;
        s->plane[i].subband[8] = s->plane[i].dwt_buf + 1 * w2 * h2;
        s->plane[i].subband[9] = s->plane[i].dwt_buf + 3 * w2 * h2;

        for (int j = 0; j < DWT_LEVELS; j++) {
            for (int k = 0; k < FF_ARRAY_ELEMS(s->plane[i].band[j]); k++) {
                s->plane[i].band[j][k].width  = (width / 8) << j;
                s->plane[i].band[j][k].height = height >> (DWT_LEVELS - j);
                s->plane[i].band[j][k].a_width  = w8 << j;
                s->plane[i].band[j][k].a_height = h8 << j;
            }
        }

        /* ll2 and ll1 commented out because they are done in-place */
        s->plane[i].l_h[0] = s->plane[i].dwt_tmp;
        s->plane[i].l_h[1] = s->plane[i].dwt_tmp + 2 * w8 * h8;
        // s->plane[i].l_h[2] = ll2;
        s->plane[i].l_h[3] = s->plane[i].dwt_tmp;
        s->plane[i].l_h[4] = s->plane[i].dwt_tmp + 2 * w4 * h4;
        // s->plane[i].l_h[5] = ll1;
        s->plane[i].l_h[6] = s->plane[i].dwt_tmp;
        s->plane[i].l_h[7] = s->plane[i].dwt_tmp + 2 * w2 * h2;
    }

    for (int i = 0; i < 512; i++) {
        int value = (i & sign_mask) ? twos_complement + (i & mag_mask): i;
        int mag = FFMIN(FFABS(value), 255);

        if (mag) {
            s->cb[i].bits = (codebook[mag][1] << 1) | (value > 0 ? 0 : 1);
            s->cb[i].size = codebook[mag][0] + 1;
        } else {
            s->cb[i].bits = codebook[mag][1];
            s->cb[i].size = codebook[mag][0];
        }
    }

    s->cb[512].bits = 0x3114ba3;
    s->cb[512].size = 26;

    s->rb[0].run = 0;

    for (int i = 1, j = 0; i < 320 && j < 17; j++) {
        int run = runbook[j][2];
        int end = runbook[j+1][2];

        while (i < end) {
            s->rb[i].run = run;
            s->rb[i].bits = runbook[j][1];
            s->rb[i++].size = runbook[j][0];
        }
    }

    s->rb[320].bits = runbook[17][1];
    s->rb[320].size = runbook[17][0];
    s->rb[320].run = 320;

    for (int i = 0; i < 256; i++) {
        int idx = i + ((768LL * i * i * i) / (256 * 256 * 256));

        s->lut[idx] = i;
    }
    for (int i = 0; i < 1024; i++) {
        if (s->lut[i])
            last = s->lut[i];
        else
            s->lut[i] = last;
    }

    ff_cfhdencdsp_init(&s->dsp);

    if (s->planes != 4)
        return 0;

    s->alpha = av_calloc(avctx->width * avctx->height, sizeof(*s->alpha));
    if (!s->alpha)
        return AVERROR(ENOMEM);

    return 0;
}

static void quantize_band(int16_t *input, int width, int a_width,
                          int height, unsigned quantization)
{
    const int16_t factor = (uint32_t)(1U << 15) / quantization;

    for (int i = 0; i < height; i++) {
        for (int j = 0; j < width; j++)
            input[j] = av_clip_intp2(((input[j] * factor + 16384 * FFSIGN(input[j])) / 32768), 10);
        input += a_width;
    }
}

static int put_runcode(PutBitContext *pb, int count, const Runbook *const rb)
{
    while (count > 0) {
        const int index = FFMIN(320, count);

        put_bits(pb, rb[index].size, rb[index].bits);
        count -= rb[index].run;
    }

    return 0;
}

static void process_alpha(const int16_t *src, int width, int height, ptrdiff_t stride, int16_t *dst)
{
    for (int i = 0; i < height; i++) {
        for (int j = 0; j < width; j++) {
            int alpha = src[j];

            if (alpha > 0 && alpha < 4080) {
                alpha *= 223;
                alpha += 128;
                alpha >>= 8;
                alpha += 256;
            }

            dst[j] = av_clip_uintp2(alpha, 12);
        }

        src += stride;
        dst += width;
    }
}

static int cfhd_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                             const AVFrame *frame, int *got_packet)
{
    CFHDEncContext *s = avctx->priv_data;
    CFHDEncDSPContext *dsp = &s->dsp;
    PutByteContext *pby = &s->pby;
    PutBitContext *pb = &s->pb;
    const Codebook *const cb = s->cb;
    const Runbook *const rb = s->rb;
    const uint16_t *lut = s->lut;
    unsigned pos;
    int ret;

    for (int plane = 0; plane < s->planes; plane++) {
        const int h_shift = plane ? s->chroma_h_shift : 0;
        int width = s->plane[plane].band[2][0].width;
        int a_width = s->plane[plane].band[2][0].a_width;
        int height = s->plane[plane].band[2][0].height;
        int act_plane = plane == 1 ? 2 : plane == 2 ? 1 : plane;
        const int16_t *input = (int16_t *)frame->data[act_plane];
        int16_t *buf;
        int16_t *low = s->plane[plane].l_h[6];
        int16_t *high = s->plane[plane].l_h[7];
        ptrdiff_t in_stride = frame->linesize[act_plane] / 2;
        int low_stride, high_stride;

        if (plane == 3) {
            process_alpha(input, avctx->width, avctx->height,
                          in_stride, s->alpha);
            input = s->alpha;
            in_stride = avctx->width;
        }

        dsp->horiz_filter(input, low, high,
                          in_stride, a_width, a_width,
                          avctx->width >> h_shift, avctx->height);

        input = s->plane[plane].l_h[7];
        low = s->plane[plane].subband[7];
        low_stride = s->plane[plane].band[2][0].a_width;
        high = s->plane[plane].subband[9];
        high_stride = s->plane[plane].band[2][0].a_width;

        dsp->vert_filter(input, low, high,
                         a_width, low_stride, high_stride,
                         width, height * 2);

        input = s->plane[plane].l_h[6];
        low = s->plane[plane].l_h[7];
        high = s->plane[plane].subband[8];

        dsp->vert_filter(input, low, high,
                         a_width, low_stride, high_stride,
                         width, height * 2);

        a_width = s->plane[plane].band[1][0].a_width;
        width = s->plane[plane].band[1][0].width;
        height = s->plane[plane].band[1][0].height;
        input = s->plane[plane].l_h[7];
        low = s->plane[plane].l_h[3];
        low_stride = s->plane[plane].band[1][0].a_width;
        high = s->plane[plane].l_h[4];
        high_stride = s->plane[plane].band[1][0].a_width;

        buf = s->plane[plane].l_h[7];
        for (int i = 0; i < height * 2; i++) {
            for (int j = 0; j < width * 2; j++)
                buf[j] /= 4;
            buf += a_width * 2;
        }

        dsp->horiz_filter(input, low, high,
                          a_width * 2, low_stride, high_stride,
                          width * 2, height * 2);

        input = s->plane[plane].l_h[4];
        low = s->plane[plane].subband[4];
        high = s->plane[plane].subband[6];

        dsp->vert_filter(input, low, high,
                         a_width, low_stride, high_stride,
                         width, height * 2);

        input = s->plane[plane].l_h[3];
        low = s->plane[plane].l_h[4];
        high = s->plane[plane].subband[5];

        dsp->vert_filter(input, low, high,
                         a_width, low_stride, high_stride,
                         width, height * 2);

        a_width = s->plane[plane].band[0][0].a_width;
        width = s->plane[plane].band[0][0].width;
        height = s->plane[plane].band[0][0].height;
        input = s->plane[plane].l_h[4];
        low = s->plane[plane].l_h[0];
        low_stride = s->plane[plane].band[0][0].a_width;
        high = s->plane[plane].l_h[1];
        high_stride = s->plane[plane].band[0][0].a_width;

        if (avctx->pix_fmt != AV_PIX_FMT_YUV422P10) {
            int16_t *buf = s->plane[plane].l_h[4];
            for (int i = 0; i < height * 2; i++) {
                for (int j = 0; j < width * 2; j++)
                    buf[j] /= 4;
                buf += a_width * 2;
            }
        }

        dsp->horiz_filter(input, low, high,
                          a_width * 2, low_stride, high_stride,
                          width * 2, height * 2);

        low = s->plane[plane].subband[1];
        high = s->plane[plane].subband[3];
        input = s->plane[plane].l_h[1];

        dsp->vert_filter(input, low, high,
                         a_width, low_stride, high_stride,
                         width, height * 2);

        low = s->plane[plane].subband[0];
        high = s->plane[plane].subband[2];
        input = s->plane[plane].l_h[0];

        dsp->vert_filter(input, low, high,
                         a_width, low_stride, high_stride,
                         width, height * 2);
    }

    ret = ff_alloc_packet(avctx, pkt, 256LL + s->planes * (4LL * avctx->width * (avctx->height + 15) + 2048LL));
    if (ret < 0)
        return ret;

    bytestream2_init_writer(pby, pkt->data, pkt->size);

    bytestream2_put_be16(pby, SampleType);
    bytestream2_put_be16(pby, 9);

    bytestream2_put_be16(pby, SampleIndexTable);
    bytestream2_put_be16(pby, s->planes);

    for (int i = 0; i < s->planes; i++)
        bytestream2_put_be32(pby, 0);

    bytestream2_put_be16(pby, TransformType);
    bytestream2_put_be16(pby, 0);

    bytestream2_put_be16(pby, NumFrames);
    bytestream2_put_be16(pby, 1);

    bytestream2_put_be16(pby, ChannelCount);
    bytestream2_put_be16(pby, s->planes);

    bytestream2_put_be16(pby, EncodedFormat);
    bytestream2_put_be16(pby, avctx->pix_fmt == AV_PIX_FMT_YUV422P10 ? 1 : 3 + (s->planes == 4));

    bytestream2_put_be16(pby, WaveletCount);
    bytestream2_put_be16(pby, 3);

    bytestream2_put_be16(pby, SubbandCount);
    bytestream2_put_be16(pby, SUBBAND_COUNT);

    bytestream2_put_be16(pby, NumSpatial);
    bytestream2_put_be16(pby, 2);

    bytestream2_put_be16(pby, FirstWavelet);
    bytestream2_put_be16(pby, 3);

    bytestream2_put_be16(pby, ImageWidth);
    bytestream2_put_be16(pby, avctx->width);

    bytestream2_put_be16(pby, ImageHeight);
    bytestream2_put_be16(pby, FFALIGN(avctx->height, 8));

    bytestream2_put_be16(pby, -DisplayHeight);
    bytestream2_put_be16(pby, avctx->height);

    bytestream2_put_be16(pby, -FrameNumber);
    bytestream2_put_be16(pby, frame->pts & 0xFFFF);

    bytestream2_put_be16(pby, Precision);
    bytestream2_put_be16(pby, avctx->pix_fmt == AV_PIX_FMT_YUV422P10 ? 10 : 12);

    bytestream2_put_be16(pby, PrescaleTable);
    bytestream2_put_be16(pby, avctx->pix_fmt == AV_PIX_FMT_YUV422P10 ? 0x2000 : 0x2800);

    bytestream2_put_be16(pby, SampleFlags);
    bytestream2_put_be16(pby, 1);

    for (int p = 0; p < s->planes; p++) {
        int width = s->plane[p].band[0][0].width;
        int a_width = s->plane[p].band[0][0].a_width;
        int height = s->plane[p].band[0][0].height;
        int16_t *data = s->plane[p].subband[0];

        if (p) {
            bytestream2_put_be16(pby, SampleType);
            bytestream2_put_be16(pby, 3);

            bytestream2_put_be16(pby, ChannelNumber);
            bytestream2_put_be16(pby, p);
        }

        bytestream2_put_be16(pby, BitstreamMarker);
        bytestream2_put_be16(pby, 0x1a4a);

        pos = bytestream2_tell_p(pby);

        bytestream2_put_be16(pby, LowpassSubband);
        bytestream2_put_be16(pby, 0);

        bytestream2_put_be16(pby, NumLevels);
        bytestream2_put_be16(pby, 3);

        bytestream2_put_be16(pby, LowpassWidth);
        bytestream2_put_be16(pby, width);

        bytestream2_put_be16(pby, LowpassHeight);
        bytestream2_put_be16(pby, height);

        bytestream2_put_be16(pby, PixelOffset);
        bytestream2_put_be16(pby, 0);

        bytestream2_put_be16(pby, LowpassQuantization);
        bytestream2_put_be16(pby, 1);

        bytestream2_put_be16(pby, LowpassPrecision);
        bytestream2_put_be16(pby, 16);

        bytestream2_put_be16(pby, BitstreamMarker);
        bytestream2_put_be16(pby, 0x0f0f);

        for (int i = 0; i < height; i++) {
            for (int j = 0; j < width; j++)
                bytestream2_put_be16(pby, data[j]);
            data += a_width;
        }

        bytestream2_put_be16(pby, BitstreamMarker);
        bytestream2_put_be16(pby, 0x1b4b);

        for (int l = 0; l < 3; l++) {
            for (int i = 0; i < 3; i++) {
                s->plane[p].quantization[1 + l * 3 + i] = quantization_per_subband[avctx->pix_fmt != AV_PIX_FMT_YUV422P10][p >= 3 ? 0 : p][s->quality][l * 3 + i];
            }
        }

        for (int l = 0; l < 3; l++) {
            int a_width = s->plane[p].band[l][0].a_width;
            int width = s->plane[p].band[l][0].width;
            int stride = FFALIGN(width, 8);
            int height = s->plane[p].band[l][0].height;

            bytestream2_put_be16(pby, BitstreamMarker);
            bytestream2_put_be16(pby, 0x0d0d);

            bytestream2_put_be16(pby, WaveletType);
            bytestream2_put_be16(pby, 3 + 2 * (l == 2));

            bytestream2_put_be16(pby, WaveletNumber);
            bytestream2_put_be16(pby, 3 - l);

            bytestream2_put_be16(pby, WaveletLevel);
            bytestream2_put_be16(pby, 3 - l);

            bytestream2_put_be16(pby, NumBands);
            bytestream2_put_be16(pby, 4);

            bytestream2_put_be16(pby, HighpassWidth);
            bytestream2_put_be16(pby, width);

            bytestream2_put_be16(pby, HighpassHeight);
            bytestream2_put_be16(pby, height);

            bytestream2_put_be16(pby, LowpassBorder);
            bytestream2_put_be16(pby, 0);

            bytestream2_put_be16(pby, HighpassBorder);
            bytestream2_put_be16(pby, 0);

            bytestream2_put_be16(pby, LowpassScale);
            bytestream2_put_be16(pby, 1);

            bytestream2_put_be16(pby, LowpassDivisor);
            bytestream2_put_be16(pby, 1);

            for (int i = 0; i < 3; i++) {
                int16_t *data = s->plane[p].subband[1 + l * 3 + i];
                int count = 0, padd = 0;

                bytestream2_put_be16(pby, BitstreamMarker);
                bytestream2_put_be16(pby, 0x0e0e);

                bytestream2_put_be16(pby, SubbandNumber);
                bytestream2_put_be16(pby, i + 1);

                bytestream2_put_be16(pby, BandCodingFlags);
                bytestream2_put_be16(pby, 1);

                bytestream2_put_be16(pby, BandWidth);
                bytestream2_put_be16(pby, width);

                bytestream2_put_be16(pby, BandHeight);
                bytestream2_put_be16(pby, height);

                bytestream2_put_be16(pby, SubbandBand);
                bytestream2_put_be16(pby, 1 + l * 3 + i);

                bytestream2_put_be16(pby, BandEncoding);
                bytestream2_put_be16(pby, 3);

                bytestream2_put_be16(pby, Quantization);
                bytestream2_put_be16(pby, s->plane[p].quantization[1 + l * 3 + i]);

                bytestream2_put_be16(pby, BandScale);
                bytestream2_put_be16(pby, 1);

                bytestream2_put_be16(pby, BandHeader);
                bytestream2_put_be16(pby, 0);

                quantize_band(data, width, a_width, height,
                              s->plane[p].quantization[1 + l * 3 + i]);

                init_put_bits(pb, pkt->data + bytestream2_tell_p(pby), bytestream2_get_bytes_left_p(pby));

                for (int m = 0; m < height; m++) {
                    for (int j = 0; j < stride; j++) {
                        int16_t index = j >= width ? 0 : FFSIGN(data[j]) * lut[FFABS(data[j])];

                        if (index < 0)
                            index += 512;
                        if (index == 0) {
                            count++;
                            continue;
                        } else if (count > 0) {
                            count = put_runcode(pb, count, rb);
                        }
                        put_bits(pb, cb[index].size, cb[index].bits);
                    }

                    data += a_width;
                }

                if (count > 0) {
                    count = put_runcode(pb, count, rb);
                }

                put_bits(pb, cb[512].size, cb[512].bits);

                flush_put_bits(pb);
                bytestream2_skip_p(pby, put_bytes_output(pb));
                padd = (4 - (bytestream2_tell_p(pby) & 3)) & 3;
                while (padd--)
                    bytestream2_put_byte(pby, 0);

                bytestream2_put_be16(pby, BandTrailer);
                bytestream2_put_be16(pby, 0);
            }

            bytestream2_put_be16(pby, BitstreamMarker);
            bytestream2_put_be16(pby, 0x0c0c);
        }

        s->plane[p].size = bytestream2_tell_p(pby) - pos;
    }

    bytestream2_put_be16(pby, GroupTrailer);
    bytestream2_put_be16(pby, 0);

    av_shrink_packet(pkt, bytestream2_tell_p(pby));

    pkt->flags |= AV_PKT_FLAG_KEY;

    bytestream2_seek_p(pby, 8, SEEK_SET);
    for (int i = 0; i < s->planes; i++)
        bytestream2_put_be32(pby, s->plane[i].size);

    *got_packet = 1;

    return 0;
}

static av_cold int cfhd_encode_close(AVCodecContext *avctx)
{
    CFHDEncContext *s = avctx->priv_data;

    for (int i = 0; i < s->planes; i++) {
        av_freep(&s->plane[i].dwt_buf);
        av_freep(&s->plane[i].dwt_tmp);

        for (int j = 0; j < SUBBAND_COUNT; j++)
            s->plane[i].subband[j] = NULL;

        for (int j = 0; j < 8; j++)
            s->plane[i].l_h[j] = NULL;
    }

    av_freep(&s->alpha);

    return 0;
}

#define OFFSET(x) offsetof(CFHDEncContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "quality", "set quality", OFFSET(quality), AV_OPT_TYPE_INT,   {.i64= 0}, 0, 12, VE, .unit = "q" },
    { "film3+",   NULL,         0,               AV_OPT_TYPE_CONST, {.i64= 0}, 0,  0, VE, .unit = "q" },
    { "film3",    NULL,         0,               AV_OPT_TYPE_CONST, {.i64= 1}, 0,  0, VE, .unit = "q" },
    { "film2+",   NULL,         0,               AV_OPT_TYPE_CONST, {.i64= 2}, 0,  0, VE, .unit = "q" },
    { "film2",    NULL,         0,               AV_OPT_TYPE_CONST, {.i64= 3}, 0,  0, VE, .unit = "q" },
    { "film1.5",  NULL,         0,               AV_OPT_TYPE_CONST, {.i64= 4}, 0,  0, VE, .unit = "q" },
    { "film1+",   NULL,         0,               AV_OPT_TYPE_CONST, {.i64= 5}, 0,  0, VE, .unit = "q" },
    { "film1",    NULL,         0,               AV_OPT_TYPE_CONST, {.i64= 6}, 0,  0, VE, .unit = "q" },
    { "high+",    NULL,         0,               AV_OPT_TYPE_CONST, {.i64= 7}, 0,  0, VE, .unit = "q" },
    { "high",     NULL,         0,               AV_OPT_TYPE_CONST, {.i64= 8}, 0,  0, VE, .unit = "q" },
    { "medium+",  NULL,         0,               AV_OPT_TYPE_CONST, {.i64= 9}, 0,  0, VE, .unit = "q" },
    { "medium",   NULL,         0,               AV_OPT_TYPE_CONST, {.i64=10}, 0,  0, VE, .unit = "q" },
    { "low+",     NULL,         0,               AV_OPT_TYPE_CONST, {.i64=11}, 0,  0, VE, .unit = "q" },
    { "low",      NULL,         0,               AV_OPT_TYPE_CONST, {.i64=12}, 0,  0, VE, .unit = "q" },
    { NULL},
};

static const AVClass cfhd_class = {
    .class_name = "cfhd",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_cfhd_encoder = {
    .p.name           = "cfhd",
    CODEC_LONG_NAME("GoPro CineForm HD"),
    .p.type           = AVMEDIA_TYPE_VIDEO,
    .p.id             = AV_CODEC_ID_CFHD,
    .p.capabilities   = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_FRAME_THREADS |
                        AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    .priv_data_size   = sizeof(CFHDEncContext),
    .p.priv_class     = &cfhd_class,
    .init             = cfhd_encode_init,
    .close            = cfhd_encode_close,
    FF_CODEC_ENCODE_CB(cfhd_encode_frame),
    .p.pix_fmts       = (const enum AVPixelFormat[]) {
                          AV_PIX_FMT_YUV422P10,
                          AV_PIX_FMT_GBRP12,
                          AV_PIX_FMT_GBRAP12,
                          AV_PIX_FMT_NONE
                        },
    .color_ranges     = AVCOL_RANGE_MPEG,
    .caps_internal    = FF_CODEC_CAP_INIT_CLEANUP,
};
