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

#include "libavutil/internal.h"
#include "libavutil/pixdesc.h"
#include "avcodec.h"
#include "get_bits.h"
#include "internal.h"
#include "put_bits.h"
#include "simple_idct.h"
#include "dvdata.h"
#include "dv.h"

/* XXX: also include quantization */
RL_VLC_ELEM ff_dv_rl_vlc[1184];

static inline void dv_calc_mb_coordinates(const DVprofile *d, int chan, int seq, int slot,
                                          uint16_t *tbl)
{
    static const uint8_t off[] = { 2, 6, 8, 0, 4 };
    static const uint8_t shuf1[] = { 36, 18, 54, 0, 72 };
    static const uint8_t shuf2[] = { 24, 12, 36, 0, 48 };
    static const uint8_t shuf3[] = { 18, 9, 27, 0, 36 };

    static const uint8_t l_start[] = {0, 4, 9, 13, 18, 22, 27, 31, 36, 40};
    static const uint8_t l_start_shuffled[] = { 9, 4, 13, 0, 18 };

    static const uint8_t serpent1[] = {0, 1, 2, 2, 1, 0,
                                       0, 1, 2, 2, 1, 0,
                                       0, 1, 2, 2, 1, 0,
                                       0, 1, 2, 2, 1, 0,
                                       0, 1, 2};
    static const uint8_t serpent2[] = {0, 1, 2, 3, 4, 5, 5, 4, 3, 2, 1, 0,
                                       0, 1, 2, 3, 4, 5, 5, 4, 3, 2, 1, 0,
                                       0, 1, 2, 3, 4, 5};

    static const uint8_t remap[][2] = {{ 0, 0}, { 0, 0}, { 0, 0}, { 0, 0}, /* dummy */
                                       { 0, 0}, { 0, 1}, { 0, 2}, { 0, 3}, {10, 0},
                                       {10, 1}, {10, 2}, {10, 3}, {20, 0}, {20, 1},
                                       {20, 2}, {20, 3}, {30, 0}, {30, 1}, {30, 2},
                                       {30, 3}, {40, 0}, {40, 1}, {40, 2}, {40, 3},
                                       {50, 0}, {50, 1}, {50, 2}, {50, 3}, {60, 0},
                                       {60, 1}, {60, 2}, {60, 3}, {70, 0}, {70, 1},
                                       {70, 2}, {70, 3}, { 0,64}, { 0,65}, { 0,66},
                                       {10,64}, {10,65}, {10,66}, {20,64}, {20,65},
                                       {20,66}, {30,64}, {30,65}, {30,66}, {40,64},
                                       {40,65}, {40,66}, {50,64}, {50,65}, {50,66},
                                       {60,64}, {60,65}, {60,66}, {70,64}, {70,65},
                                       {70,66}, { 0,67}, {20,67}, {40,67}, {60,67}};

    int i, k, m;
    int x, y, blk;

    for (m=0; m<5; m++) {
         switch (d->width) {
         case 1440:
              blk = (chan*11+seq)*27+slot;

              if (chan == 0 && seq == 11) {
                  x = m*27+slot;
                  if (x<90) {
                      y = 0;
                  } else {
                      x = (x - 90)*2;
                      y = 67;
                  }
              } else {
                  i = (4*chan + blk + off[m])%11;
                  k = (blk/11)%27;

                  x = shuf1[m] + (chan&1)*9 + k%9;
                  y = (i*3+k/9)*2 + (chan>>1) + 1;
              }
              tbl[m] = (x<<1)|(y<<9);
              break;
         case 1280:
              blk = (chan*10+seq)*27+slot;

              i = (4*chan + (seq/5) + 2*blk + off[m])%10;
              k = (blk/5)%27;

              x = shuf1[m]+(chan&1)*9 + k%9;
              y = (i*3+k/9)*2 + (chan>>1) + 4;

              if (x >= 80) {
                  x = remap[y][0]+((x-80)<<(y>59));
                  y = remap[y][1];
              }
              tbl[m] = (x<<1)|(y<<9);
              break;
       case 960:
              blk = (chan*10+seq)*27+slot;

              i = (4*chan + (seq/5) + 2*blk + off[m])%10;
              k = (blk/5)%27 + (i&1)*3;

              x = shuf2[m] + k%6 + 6*(chan&1);
              y = l_start[i] + k/6 + 45*(chan>>1);
              tbl[m] = (x<<1)|(y<<9);
              break;
        case 720:
              switch (d->pix_fmt) {
              case AV_PIX_FMT_YUV422P:
                   x = shuf3[m] + slot/3;
                   y = serpent1[slot] +
                       ((((seq + off[m]) % d->difseg_size)<<1) + chan)*3;
                   tbl[m] = (x<<1)|(y<<8);
                   break;
              case AV_PIX_FMT_YUV420P:
                   x = shuf3[m] + slot/3;
                   y = serpent1[slot] +
                       ((seq + off[m]) % d->difseg_size)*3;
                   tbl[m] = (x<<1)|(y<<9);
                   break;
              case AV_PIX_FMT_YUV411P:
                   i = (seq + off[m]) % d->difseg_size;
                   k = slot + ((m==1||m==2)?3:0);

                   x = l_start_shuffled[m] + k/6;
                   y = serpent2[k] + i*6;
                   if (x>21)
                       y = y*2 - i*6;
                   tbl[m] = (x<<2)|(y<<8);
                   break;
              }
        default:
              break;
        }
    }
}

/* quantization quanta by QNO for DV100 */
static const uint8_t dv100_qstep[16] = {
    1, /* QNO = 0 and 1 both have no quantization */
    1,
    2, 3, 4, 5, 6, 7, 8, 16, 18, 20, 22, 24, 28, 52
};

static const uint8_t dv_quant_areas[4]  = { 6, 21, 43, 64 };

int ff_dv_init_dynamic_tables(const DVprofile *d)
{
    int j,i,c,s,p;
    uint32_t *factor1, *factor2;
    const int *iweight1, *iweight2;

    if (!d->work_chunks[dv_work_pool_size(d)-1].buf_offset) {
        p = i = 0;
        for (c=0; c<d->n_difchan; c++) {
            for (s=0; s<d->difseg_size; s++) {
                p += 6;
                for (j=0; j<27; j++) {
                    p += !(j%3);
                    if (!(DV_PROFILE_IS_1080i50(d) && c != 0 && s == 11) &&
                        !(DV_PROFILE_IS_720p50(d) && s > 9)) {
                          dv_calc_mb_coordinates(d, c, s, j, &d->work_chunks[i].mb_coordinates[0]);
                          d->work_chunks[i++].buf_offset = p;
                    }
                    p += 5;
                }
            }
        }
    }

    if (!d->idct_factor[DV_PROFILE_IS_HD(d)?8191:5631]) {
        factor1 = &d->idct_factor[0];
        factor2 = &d->idct_factor[DV_PROFILE_IS_HD(d)?4096:2816];
        if (d->height == 720) {
            iweight1 = &ff_dv_iweight_720_y[0];
            iweight2 = &ff_dv_iweight_720_c[0];
        } else {
            iweight1 = &ff_dv_iweight_1080_y[0];
            iweight2 = &ff_dv_iweight_1080_c[0];
        }
        if (DV_PROFILE_IS_HD(d)) {
            for (c = 0; c < 4; c++) {
                for (s = 0; s < 16; s++) {
                    for (i = 0; i < 64; i++) {
                        *factor1++ = (dv100_qstep[s] << (c + 9)) * iweight1[i];
                        *factor2++ = (dv100_qstep[s] << (c + 9)) * iweight2[i];
                    }
                }
            }
        } else {
            iweight1 = &ff_dv_iweight_88[0];
            for (j = 0; j < 2; j++, iweight1 = &ff_dv_iweight_248[0]) {
                for (s = 0; s < 22; s++) {
                    for (i = c = 0; c < 4; c++) {
                        for (; i < dv_quant_areas[c]; i++) {
                            *factor1   = iweight1[i] << (ff_dv_quant_shifts[s][c] + 1);
                            *factor2++ = (*factor1++) << 1;
                        }
                    }
                }
            }
        }
    }

    return 0;
}

av_cold int ff_dvvideo_init(AVCodecContext *avctx)
{
    DVVideoContext *s = avctx->priv_data;
    DSPContext dsp;
    static int done = 0;
    int i, j;

    if (!done) {
        VLC dv_vlc;
        uint16_t new_dv_vlc_bits[NB_DV_VLC*2];
        uint8_t  new_dv_vlc_len[NB_DV_VLC*2];
        uint8_t  new_dv_vlc_run[NB_DV_VLC*2];
        int16_t  new_dv_vlc_level[NB_DV_VLC*2];

        done = 1;

        /* it's faster to include sign bit in a generic VLC parsing scheme */
        for (i = 0, j = 0; i < NB_DV_VLC; i++, j++) {
            new_dv_vlc_bits[j]  = ff_dv_vlc_bits[i];
            new_dv_vlc_len[j]   = ff_dv_vlc_len[i];
            new_dv_vlc_run[j]   = ff_dv_vlc_run[i];
            new_dv_vlc_level[j] = ff_dv_vlc_level[i];

            if (ff_dv_vlc_level[i]) {
                new_dv_vlc_bits[j] <<= 1;
                new_dv_vlc_len[j]++;

                j++;
                new_dv_vlc_bits[j]  = (ff_dv_vlc_bits[i] << 1) | 1;
                new_dv_vlc_len[j]   =  ff_dv_vlc_len[i] + 1;
                new_dv_vlc_run[j]   =  ff_dv_vlc_run[i];
                new_dv_vlc_level[j] = -ff_dv_vlc_level[i];
            }
        }

        /* NOTE: as a trick, we use the fact the no codes are unused
           to accelerate the parsing of partial codes */
        init_vlc(&dv_vlc, TEX_VLC_BITS, j,
                 new_dv_vlc_len, 1, 1, new_dv_vlc_bits, 2, 2, 0);
        av_assert1(dv_vlc.table_size == 1184);

        for (i = 0; i < dv_vlc.table_size; i++){
            int code = dv_vlc.table[i][0];
            int len  = dv_vlc.table[i][1];
            int level, run;

            if (len < 0){ //more bits needed
                run   = 0;
                level = code;
            } else {
                run   = new_dv_vlc_run  [code] + 1;
                level = new_dv_vlc_level[code];
            }
            ff_dv_rl_vlc[i].len   = len;
            ff_dv_rl_vlc[i].level = level;
            ff_dv_rl_vlc[i].run   = run;
        }
        ff_free_vlc(&dv_vlc);
    }

    /* Generic DSP setup */
    memset(&dsp,0, sizeof(dsp));
    ff_dsputil_init(&dsp, avctx);
    ff_set_cmp(&dsp, dsp.ildct_cmp, avctx->ildct_cmp);
    s->get_pixels = dsp.get_pixels;
    s->ildct_cmp = dsp.ildct_cmp[5];

    /* 88DCT setup */
    s->fdct[0]     = dsp.fdct;
    s->idct_put[0] = dsp.idct_put;
    for (i = 0; i < 64; i++)
       s->dv_zigzag[0][i] = dsp.idct_permutation[ff_zigzag_direct[i]];

    /* 248DCT setup */
    s->fdct[1]     = dsp.fdct248;
    s->idct_put[1] = ff_simple_idct248_put;  // FIXME: need to add it to DSP
    if (avctx->lowres){
        for (i = 0; i < 64; i++){
            int j = ff_dv_zigzag248_direct[i];
            s->dv_zigzag[1][i] = dsp.idct_permutation[(j & 7) + (j & 8) * 4 + (j & 48) / 2];
        }
    }else
        memcpy(s->dv_zigzag[1], ff_dv_zigzag248_direct, 64);

    s->avctx = avctx;
    avctx->chroma_sample_location = AVCHROMA_LOC_TOPLEFT;

    return 0;
}

