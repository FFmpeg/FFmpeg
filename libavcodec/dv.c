/*
 * DV decoder
 * Copyright (c) 2002 Fabrice Bellard
 * Copyright (c) 2004 Roman Shaposhnik
 *
 * DV encoder
 * Copyright (c) 2003 Roman Shaposhnik
 *
 * 50 Mbps (DVCPRO50) support
 * Copyright (c) 2006 Daniel Maas <dmaas@maasdigital.com>
 *
 * 100 Mbps (DVCPRO HD) support
 * Initial code by Daniel Maas <dmaas@maasdigital.com> (funded by BBC R&D)
 * Final code by Roman Shaposhnik
 *
 * Many thanks to Dan Dennedy <dan@dennedy.org> for providing wealth
 * of DV technical info.
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
 * DV codec.
 */

#include "avcodec.h"
#include "dv.h"

static inline void dv_calc_mb_coordinates(const AVDVProfile *d, int chan,
                                          int seq, int slot, uint16_t *tbl)
{
    static const uint8_t off[]   = {  2,  6,  8, 0,  4 };
    static const uint8_t shuf1[] = { 36, 18, 54, 0, 72 };
    static const uint8_t shuf2[] = { 24, 12, 36, 0, 48 };
    static const uint8_t shuf3[] = { 18,  9, 27, 0, 36 };

    static const uint8_t l_start[]          = { 0, 4, 9, 13, 18, 22, 27, 31, 36, 40 };
    static const uint8_t l_start_shuffled[] = { 9, 4, 13, 0, 18 };

    static const uint8_t serpent1[] = {
        0, 1, 2, 2, 1, 0,
        0, 1, 2, 2, 1, 0,
        0, 1, 2, 2, 1, 0,
        0, 1, 2, 2, 1, 0,
        0, 1, 2
    };
    static const uint8_t serpent2[] = {
        0, 1, 2, 3, 4, 5, 5, 4, 3, 2, 1, 0,
        0, 1, 2, 3, 4, 5, 5, 4, 3, 2, 1, 0,
        0, 1, 2, 3, 4, 5
    };

    static const uint8_t remap[][2] = {
        {  0,  0 }, {  0,  0 }, {  0,  0 }, {  0,  0 }, /* dummy */
        {  0,  0 }, {  0,  1 }, {  0,  2 }, {  0,  3 }, { 10,  0 },
        { 10,  1 }, { 10,  2 }, { 10,  3 }, { 20,  0 }, { 20,  1 },
        { 20,  2 }, { 20,  3 }, { 30,  0 }, { 30,  1 }, { 30,  2 },
        { 30,  3 }, { 40,  0 }, { 40,  1 }, { 40,  2 }, { 40,  3 },
        { 50,  0 }, { 50,  1 }, { 50,  2 }, { 50,  3 }, { 60,  0 },
        { 60,  1 }, { 60,  2 }, { 60,  3 }, { 70,  0 }, { 70,  1 },
        { 70,  2 }, { 70,  3 }, {  0, 64 }, {  0, 65 }, {  0, 66 },
        { 10, 64 }, { 10, 65 }, { 10, 66 }, { 20, 64 }, { 20, 65 },
        { 20, 66 }, { 30, 64 }, { 30, 65 }, { 30, 66 }, { 40, 64 },
        { 40, 65 }, { 40, 66 }, { 50, 64 }, { 50, 65 }, { 50, 66 },
        { 60, 64 }, { 60, 65 }, { 60, 66 }, { 70, 64 }, { 70, 65 },
        { 70, 66 }, {  0, 67 }, { 20, 67 }, { 40, 67 }, { 60, 67 }
    };

    int i, k, m;
    int x, y, blk;

    for (m = 0; m < 5; m++) {
        switch (d->width) {
        case 1440:
            blk = (chan * 11 + seq) * 27 + slot;

            if (chan == 0 && seq == 11) {
                x = m * 27 + slot;
                if (x < 90) {
                    y = 0;
                } else {
                    x = (x - 90) * 2;
                    y = 67;
                }
            } else {
                i = (4 * chan + blk + off[m]) % 11;
                k = (blk / 11) % 27;

                x = shuf1[m] + (chan & 1) * 9 + k % 9;
                y = (i * 3 + k / 9) * 2 + (chan >> 1) + 1;
            }
            tbl[m] = (x << 1) | (y << 9);
            break;
        case 1280:
            blk = (chan * 10 + seq) * 27 + slot;

            i = (4 * chan + (seq / 5) + 2 * blk + off[m]) % 10;
            k = (blk / 5) % 27;

            x = shuf1[m] + (chan & 1) * 9 + k % 9;
            y = (i * 3 + k / 9) * 2 + (chan >> 1) + 4;

            if (x >= 80) {
                x = remap[y][0] + ((x - 80) << (y > 59));
                y = remap[y][1];
            }
            tbl[m] = (x << 1) | (y << 9);
            break;
        case 960:
            blk = (chan * 10 + seq) * 27 + slot;

            i = (4 * chan + (seq / 5) + 2 * blk + off[m]) % 10;
            k = (blk / 5) % 27 + (i & 1) * 3;

            x      = shuf2[m]   + k % 6 +  6 * (chan  & 1);
            y      = l_start[i] + k / 6 + 45 * (chan >> 1);
            tbl[m] = (x << 1) | (y << 9);
            break;
        case 720:
            switch (d->pix_fmt) {
            case AV_PIX_FMT_YUV422P:
                x = shuf3[m] + slot / 3;
                y = serpent1[slot] +
                    ((((seq + off[m]) % d->difseg_size) << 1) + chan) * 3;
                tbl[m] = (x << 1) | (y << 8);
                break;
            case AV_PIX_FMT_YUV420P:
                x = shuf3[m] + slot / 3;
                y = serpent1[slot] +
                    ((seq + off[m]) % d->difseg_size) * 3;
                tbl[m] = (x << 1) | (y << 9);
                break;
            case AV_PIX_FMT_YUV411P:
                i = (seq + off[m]) % d->difseg_size;
                k = slot + ((m == 1 || m == 2) ? 3 : 0);

                x = l_start_shuffled[m] + k / 6;
                y = serpent2[k] + i * 6;
                if (x > 21)
                    y = y * 2 - i * 6;
                tbl[m] = (x << 2) | (y << 8);
                break;
            }
        default:
            break;
        }
    }
}

int ff_dv_init_dynamic_tables(DVVideoContext *ctx, const AVDVProfile *d)
{
    int j, i, c, s, p;

    p = i = 0;
    for (c = 0; c < d->n_difchan; c++) {
        for (s = 0; s < d->difseg_size; s++) {
            p += 6;
            for (j = 0; j < 27; j++) {
                p += !(j % 3);
                if (!(DV_PROFILE_IS_1080i50(d) && c != 0 && s == 11) &&
                    !(DV_PROFILE_IS_720p50(d) && s > 9)) {
                    dv_calc_mb_coordinates(d, c, s, j, &ctx->work_chunks[i].mb_coordinates[0]);
                    ctx->work_chunks[i++].buf_offset = p;
                }
                p += 5;
            }
        }
    }

    return 0;
}

av_cold int ff_dvvideo_init(AVCodecContext *avctx)
{
    DVVideoContext *s = avctx->priv_data;

    s->avctx = avctx;
    avctx->chroma_sample_location = AVCHROMA_LOC_TOPLEFT;

    return 0;
}
