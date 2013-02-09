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
#include "dsputil.h"
#include "get_bits.h"
#include "internal.h"
#include "put_bits.h"
#include "simple_idct.h"
#include "dvdata.h"
#include "dv_tablegen.h"

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
            new_dv_vlc_bits[j]  = dv_vlc_bits[i];
            new_dv_vlc_len[j]   = dv_vlc_len[i];
            new_dv_vlc_run[j]   = dv_vlc_run[i];
            new_dv_vlc_level[j] = dv_vlc_level[i];

            if (dv_vlc_level[i]) {
                new_dv_vlc_bits[j] <<= 1;
                new_dv_vlc_len[j]++;

                j++;
                new_dv_vlc_bits[j]  = (dv_vlc_bits[i] << 1) | 1;
                new_dv_vlc_len[j]   =  dv_vlc_len[i] + 1;
                new_dv_vlc_run[j]   =  dv_vlc_run[i];
                new_dv_vlc_level[j] = -dv_vlc_level[i];
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
            int j = ff_zigzag248_direct[i];
            s->dv_zigzag[1][i] = dsp.idct_permutation[(j & 7) + (j & 8) * 4 + (j & 48) / 2];
        }
    }else
        memcpy(s->dv_zigzag[1], ff_zigzag248_direct, 64);

    avctx->coded_frame = &s->picture;
    s->avctx = avctx;
    avctx->chroma_sample_location = AVCHROMA_LOC_TOPLEFT;

    return 0;
}

static av_cold int dvvideo_init_encoder(AVCodecContext *avctx)
{
    if (!avpriv_dv_codec_profile(avctx)) {
        av_log(avctx, AV_LOG_ERROR, "Found no DV profile for %ix%i %s video. "
               "Valid DV profiles are:\n",
               avctx->width, avctx->height, av_get_pix_fmt_name(avctx->pix_fmt));
        ff_dv_print_profiles(avctx, AV_LOG_ERROR);
        return AVERROR(EINVAL);
    }
    if (avctx->height > 576) {
        av_log(avctx, AV_LOG_ERROR, "DVCPRO HD encoding is not supported.\n");
        return AVERROR_PATCHWELCOME;
    }

    dv_vlc_map_tableinit();

    return ff_dvvideo_init(avctx);
}

/* bit budget for AC only in 5 MBs */
static const int vs_total_ac_bits = (100 * 4 + 68*2) * 5;
static const int mb_area_start[5] = { 1, 6, 21, 43, 64 };

static inline int put_bits_left(PutBitContext* s)
{
    return (s->buf_end - s->buf) * 8 - put_bits_count(s);
}

#if CONFIG_SMALL
/* Converts run and level (where level != 0) pair into VLC, returning bit size */
static av_always_inline int dv_rl2vlc(int run, int level, int sign, uint32_t* vlc)
{
    int size;
    if (run < DV_VLC_MAP_RUN_SIZE && level < DV_VLC_MAP_LEV_SIZE) {
        *vlc = dv_vlc_map[run][level].vlc | sign;
        size = dv_vlc_map[run][level].size;
    }
    else {
        if (level < DV_VLC_MAP_LEV_SIZE) {
            *vlc = dv_vlc_map[0][level].vlc | sign;
            size = dv_vlc_map[0][level].size;
        } else {
            *vlc = 0xfe00 | (level << 1) | sign;
            size = 16;
        }
        if (run) {
            *vlc |= ((run < 16) ? dv_vlc_map[run-1][0].vlc :
                                  (0x1f80 | (run - 1))) << size;
            size +=  (run < 16) ? dv_vlc_map[run-1][0].size : 13;
        }
    }

    return size;
}

static av_always_inline int dv_rl2vlc_size(int run, int level)
{
    int size;

    if (run < DV_VLC_MAP_RUN_SIZE && level < DV_VLC_MAP_LEV_SIZE) {
        size = dv_vlc_map[run][level].size;
    }
    else {
        size = (level < DV_VLC_MAP_LEV_SIZE) ? dv_vlc_map[0][level].size : 16;
        if (run) {
            size += (run < 16) ? dv_vlc_map[run-1][0].size : 13;
        }
    }
    return size;
}
#else
static av_always_inline int dv_rl2vlc(int run, int l, int sign, uint32_t* vlc)
{
    *vlc = dv_vlc_map[run][l].vlc | sign;
    return dv_vlc_map[run][l].size;
}

static av_always_inline int dv_rl2vlc_size(int run, int l)
{
    return dv_vlc_map[run][l].size;
}
#endif

typedef struct EncBlockInfo {
    int      area_q[4];
    int      bit_size[4];
    int      prev[5];
    int      cur_ac;
    int      cno;
    int      dct_mode;
    int16_t  mb[64];
    uint8_t  next[64];
    uint8_t  sign[64];
    uint8_t  partial_bit_count;
    uint32_t partial_bit_buffer; /* we can't use uint16_t here */
} EncBlockInfo;

static av_always_inline PutBitContext* dv_encode_ac(EncBlockInfo* bi,
                                                    PutBitContext* pb_pool,
                                                    PutBitContext* pb_end)
{
    int prev, bits_left;
    PutBitContext* pb = pb_pool;
    int size = bi->partial_bit_count;
    uint32_t vlc = bi->partial_bit_buffer;

    bi->partial_bit_count = bi->partial_bit_buffer = 0;
    for (;;){
       /* Find suitable storage space */
       for (; size > (bits_left = put_bits_left(pb)); pb++) {
          if (bits_left) {
              size -= bits_left;
              put_bits(pb, bits_left, vlc >> size);
              vlc = vlc & ((1 << size) - 1);
          }
          if (pb + 1 >= pb_end) {
              bi->partial_bit_count  = size;
              bi->partial_bit_buffer = vlc;
              return pb;
          }
       }

       /* Store VLC */
       put_bits(pb, size, vlc);

       if (bi->cur_ac >= 64)
           break;

       /* Construct the next VLC */
       prev       = bi->cur_ac;
       bi->cur_ac = bi->next[prev];
       if (bi->cur_ac < 64){
           size = dv_rl2vlc(bi->cur_ac - prev - 1, bi->mb[bi->cur_ac], bi->sign[bi->cur_ac], &vlc);
       } else {
           size = 4; vlc = 6; /* End Of Block stamp */
       }
    }
    return pb;
}

static av_always_inline int dv_guess_dct_mode(DVVideoContext *s, uint8_t *data, int linesize) {
    if (s->avctx->flags & CODEC_FLAG_INTERLACED_DCT) {
        int ps = s->ildct_cmp(NULL, data, NULL, linesize, 8) - 400;
        if (ps > 0) {
            int is = s->ildct_cmp(NULL, data           , NULL, linesize<<1, 4) +
                     s->ildct_cmp(NULL, data + linesize, NULL, linesize<<1, 4);
            return ps > is;
        }
    }

    return 0;
}

static const int dv_weight_bits = 18;
static const int dv_weight_88[64] = {
 131072, 257107, 257107, 242189, 252167, 242189, 235923, 237536,
 237536, 235923, 229376, 231390, 223754, 231390, 229376, 222935,
 224969, 217965, 217965, 224969, 222935, 200636, 218652, 211916,
 212325, 211916, 218652, 200636, 188995, 196781, 205965, 206433,
 206433, 205965, 196781, 188995, 185364, 185364, 200636, 200704,
 200636, 185364, 185364, 174609, 180568, 195068, 195068, 180568,
 174609, 170091, 175557, 189591, 175557, 170091, 165371, 170627,
 170627, 165371, 160727, 153560, 160727, 144651, 144651, 136258,
};
static const int dv_weight_248[64] = {
 131072, 242189, 257107, 237536, 229376, 200636, 242189, 223754,
 224969, 196781, 262144, 242189, 229376, 200636, 257107, 237536,
 211916, 185364, 235923, 217965, 229376, 211916, 206433, 180568,
 242189, 223754, 224969, 196781, 211916, 185364, 235923, 217965,
 200704, 175557, 222935, 205965, 200636, 185364, 195068, 170627,
 229376, 211916, 206433, 180568, 200704, 175557, 222935, 205965,
 175557, 153560, 188995, 174609, 165371, 144651, 200636, 185364,
 195068, 170627, 175557, 153560, 188995, 174609, 165371, 144651,
};

static av_always_inline int dv_init_enc_block(EncBlockInfo* bi, uint8_t *data, int linesize, DVVideoContext *s, int bias)
{
    const int *weight;
    const uint8_t* zigzag_scan;
    LOCAL_ALIGNED_16(int16_t, blk, [64]);
    int i, area;
    /* We offer two different methods for class number assignment: the
       method suggested in SMPTE 314M Table 22, and an improved
       method. The SMPTE method is very conservative; it assigns class
       3 (i.e. severe quantization) to any block where the largest AC
       component is greater than 36. FFmpeg's DV encoder tracks AC bit
       consumption precisely, so there is no need to bias most blocks
       towards strongly lossy compression. Instead, we assign class 2
       to most blocks, and use class 3 only when strictly necessary
       (for blocks whose largest AC component exceeds 255). */

#if 0 /* SMPTE spec method */
    static const int classes[] = {12, 24, 36, 0xffff};
#else /* improved FFmpeg method */
    static const int classes[] = {-1, -1, 255, 0xffff};
#endif
    int max  = classes[0];
    int prev = 0;

    av_assert2((((int)blk) & 15) == 0);

    bi->area_q[0] = bi->area_q[1] = bi->area_q[2] = bi->area_q[3] = 0;
    bi->partial_bit_count = 0;
    bi->partial_bit_buffer = 0;
    bi->cur_ac = 0;
    if (data) {
        bi->dct_mode = dv_guess_dct_mode(s, data, linesize);
        s->get_pixels(blk, data, linesize);
        s->fdct[bi->dct_mode](blk);
    } else {
        /* We rely on the fact that encoding all zeros leads to an immediate EOB,
           which is precisely what the spec calls for in the "dummy" blocks. */
        memset(blk, 0, 64*sizeof(*blk));
        bi->dct_mode = 0;
    }
    bi->mb[0] = blk[0];

    zigzag_scan = bi->dct_mode ? ff_zigzag248_direct : ff_zigzag_direct;
    weight = bi->dct_mode ? dv_weight_248 : dv_weight_88;

    for (area = 0; area < 4; area++) {
       bi->prev[area]     = prev;
       bi->bit_size[area] = 1; // 4 areas 4 bits for EOB :)
       for (i = mb_area_start[area]; i < mb_area_start[area+1]; i++) {
          int level = blk[zigzag_scan[i]];

          if (level + 15 > 30U) {
              bi->sign[i] = (level >> 31) & 1;
              /* weight it and and shift down into range, adding for rounding */
              /* the extra division by a factor of 2^4 reverses the 8x expansion of the DCT
                 AND the 2x doubling of the weights */
              level = (FFABS(level) * weight[i] + (1 << (dv_weight_bits+3))) >> (dv_weight_bits+4);
              bi->mb[i] = level;
              if (level > max)
                  max = level;
              bi->bit_size[area] += dv_rl2vlc_size(i - prev  - 1, level);
              bi->next[prev]= i;
              prev = i;
          }
       }
    }
    bi->next[prev]= i;
    for (bi->cno = 0; max > classes[bi->cno]; bi->cno++);

    bi->cno += bias;

    if (bi->cno >= 3) {
        bi->cno = 3;
        prev    = 0;
        i       = bi->next[prev];
        for (area = 0; area < 4; area++) {
            bi->prev[area]     = prev;
            bi->bit_size[area] = 1; // 4 areas 4 bits for EOB :)
            for (; i < mb_area_start[area+1]; i = bi->next[i]) {
                bi->mb[i] >>= 1;

                if (bi->mb[i]) {
                    bi->bit_size[area] += dv_rl2vlc_size(i - prev - 1, bi->mb[i]);
                    bi->next[prev]= i;
                    prev = i;
                }
            }
        }
        bi->next[prev]= i;
    }

    return bi->bit_size[0] + bi->bit_size[1] + bi->bit_size[2] + bi->bit_size[3];
}

static inline void dv_guess_qnos(EncBlockInfo* blks, int* qnos)
{
    int size[5];
    int i, j, k, a, prev, a2;
    EncBlockInfo* b;

    size[0] = size[1] = size[2] = size[3] = size[4] = 1 << 24;
    do {
       b = blks;
       for (i = 0; i < 5; i++) {
          if (!qnos[i])
              continue;

          qnos[i]--;
          size[i] = 0;
          for (j = 0; j < 6; j++, b++) {
             for (a = 0; a < 4; a++) {
                if (b->area_q[a] != ff_dv_quant_shifts[qnos[i] + ff_dv_quant_offset[b->cno]][a]) {
                    b->bit_size[a] = 1; // 4 areas 4 bits for EOB :)
                    b->area_q[a]++;
                    prev = b->prev[a];
                    av_assert2(b->next[prev] >= mb_area_start[a+1] || b->mb[prev]);
                    for (k = b->next[prev] ; k < mb_area_start[a+1]; k = b->next[k]) {
                       b->mb[k] >>= 1;
                       if (b->mb[k]) {
                           b->bit_size[a] += dv_rl2vlc_size(k - prev - 1, b->mb[k]);
                           prev = k;
                       } else {
                           if (b->next[k] >= mb_area_start[a+1] && b->next[k]<64){
                                for (a2 = a + 1; b->next[k] >= mb_area_start[a2+1]; a2++)
                                    b->prev[a2] = prev;
                                av_assert2(a2 < 4);
                                av_assert2(b->mb[b->next[k]]);
                                b->bit_size[a2] += dv_rl2vlc_size(b->next[k] - prev - 1, b->mb[b->next[k]])
                                                  -dv_rl2vlc_size(b->next[k] -    k - 1, b->mb[b->next[k]]);
                                av_assert2(b->prev[a2] == k && (a2 + 1 >= 4 || b->prev[a2+1] != k));
                                b->prev[a2] = prev;
                           }
                           b->next[prev] = b->next[k];
                       }
                    }
                    b->prev[a+1]= prev;
                }
                size[i] += b->bit_size[a];
             }
          }
          if (vs_total_ac_bits >= size[0] + size[1] + size[2] + size[3] + size[4])
                return;
       }
    } while (qnos[0]|qnos[1]|qnos[2]|qnos[3]|qnos[4]);


    for (a = 2; a == 2 || vs_total_ac_bits < size[0]; a += a){
        b = blks;
        size[0] = 5 * 6 * 4; //EOB
        for (j = 0; j < 6 *5; j++, b++) {
            prev = b->prev[0];
            for (k = b->next[prev]; k < 64; k = b->next[k]) {
                if (b->mb[k] < a && b->mb[k] > -a){
                    b->next[prev] = b->next[k];
                }else{
                    size[0] += dv_rl2vlc_size(k - prev - 1, b->mb[k]);
                    prev = k;
                }
            }
        }
    }
}

static int dv_encode_video_segment(AVCodecContext *avctx, void *arg)
{
    DVVideoContext *s = avctx->priv_data;
    DVwork_chunk *work_chunk = arg;
    int mb_index, i, j;
    int mb_x, mb_y, c_offset, linesize, y_stride;
    uint8_t*  y_ptr;
    uint8_t*  dif;
    LOCAL_ALIGNED_8(uint8_t, scratch, [128]);
    EncBlockInfo  enc_blks[5*DV_MAX_BPM];
    PutBitContext pbs[5*DV_MAX_BPM];
    PutBitContext* pb;
    EncBlockInfo* enc_blk;
    int       vs_bit_size = 0;
    int       qnos[5] = {15, 15, 15, 15, 15}; /* No quantization */
    int*      qnosp = &qnos[0];

    dif = &s->buf[work_chunk->buf_offset*80];
    enc_blk = &enc_blks[0];
    for (mb_index = 0; mb_index < 5; mb_index++) {
        dv_calculate_mb_xy(s, work_chunk, mb_index, &mb_x, &mb_y);

        /* initializing luminance blocks */
        if ((s->sys->pix_fmt == AV_PIX_FMT_YUV420P) ||
            (s->sys->pix_fmt == AV_PIX_FMT_YUV411P && mb_x >= (704 / 8)) ||
            (s->sys->height >= 720 && mb_y != 134)) {
            y_stride = s->picture.linesize[0] << 3;
        } else {
            y_stride = 16;
        }
        y_ptr    = s->picture.data[0] + ((mb_y * s->picture.linesize[0] + mb_x) << 3);
        linesize = s->picture.linesize[0];

        if (s->sys->video_stype == 4) { /* SD 422 */
            vs_bit_size +=
            dv_init_enc_block(enc_blk+0, y_ptr               , linesize, s, 0) +
            dv_init_enc_block(enc_blk+1, NULL                , linesize, s, 0) +
            dv_init_enc_block(enc_blk+2, y_ptr + 8           , linesize, s, 0) +
            dv_init_enc_block(enc_blk+3, NULL                , linesize, s, 0);
        } else {
            vs_bit_size +=
            dv_init_enc_block(enc_blk+0, y_ptr               , linesize, s, 0) +
            dv_init_enc_block(enc_blk+1, y_ptr + 8           , linesize, s, 0) +
            dv_init_enc_block(enc_blk+2, y_ptr     + y_stride, linesize, s, 0) +
            dv_init_enc_block(enc_blk+3, y_ptr + 8 + y_stride, linesize, s, 0);
        }
        enc_blk += 4;

        /* initializing chrominance blocks */
        c_offset = (((mb_y >>  (s->sys->pix_fmt == AV_PIX_FMT_YUV420P)) * s->picture.linesize[1] +
                     (mb_x >> ((s->sys->pix_fmt == AV_PIX_FMT_YUV411P) ? 2 : 1))) << 3);
        for (j = 2; j; j--) {
            uint8_t *c_ptr = s->picture.data[j] + c_offset;
            linesize = s->picture.linesize[j];
            y_stride = (mb_y == 134) ? 8 : (s->picture.linesize[j] << 3);
            if (s->sys->pix_fmt == AV_PIX_FMT_YUV411P && mb_x >= (704 / 8)) {
                uint8_t* d;
                uint8_t* b = scratch;
                for (i = 0; i < 8; i++) {
                    d = c_ptr + (linesize << 3);
                    b[0] = c_ptr[0]; b[1] = c_ptr[1]; b[2] = c_ptr[2]; b[3] = c_ptr[3];
                    b[4] =     d[0]; b[5] =     d[1]; b[6] =     d[2]; b[7] =     d[3];
                    c_ptr += linesize;
                    b += 16;
                }
                c_ptr = scratch;
                linesize = 16;
            }

            vs_bit_size += dv_init_enc_block(    enc_blk++, c_ptr           , linesize, s, 1);
            if (s->sys->bpm == 8) {
                vs_bit_size += dv_init_enc_block(enc_blk++, c_ptr + y_stride, linesize, s, 1);
            }
        }
    }

    if (vs_total_ac_bits < vs_bit_size)
        dv_guess_qnos(&enc_blks[0], qnosp);

    /* DIF encoding process */
    for (j=0; j<5*s->sys->bpm;) {
        int start_mb = j;

        dif[3] = *qnosp++;
        dif += 4;

        /* First pass over individual cells only */
        for (i=0; i<s->sys->bpm; i++, j++) {
            int sz = s->sys->block_sizes[i]>>3;

            init_put_bits(&pbs[j], dif, sz);
            put_sbits(&pbs[j], 9, ((enc_blks[j].mb[0] >> 3) - 1024 + 2) >> 2);
            put_bits(&pbs[j], 1, enc_blks[j].dct_mode);
            put_bits(&pbs[j], 2, enc_blks[j].cno);

            dv_encode_ac(&enc_blks[j], &pbs[j], &pbs[j+1]);
            dif += sz;
        }

        /* Second pass over each MB space */
        pb = &pbs[start_mb];
        for (i=0; i<s->sys->bpm; i++) {
            if (enc_blks[start_mb+i].partial_bit_count)
                pb = dv_encode_ac(&enc_blks[start_mb+i], pb, &pbs[start_mb+s->sys->bpm]);
        }
    }

    /* Third and final pass over the whole video segment space */
    pb = &pbs[0];
    for (j=0; j<5*s->sys->bpm; j++) {
       if (enc_blks[j].partial_bit_count)
           pb = dv_encode_ac(&enc_blks[j], pb, &pbs[s->sys->bpm*5]);
       if (enc_blks[j].partial_bit_count)
            av_log(avctx, AV_LOG_ERROR, "ac bitstream overflow\n");
    }

    for (j=0; j<5*s->sys->bpm; j++) {
       int pos;
       int size = pbs[j].size_in_bits >> 3;
       flush_put_bits(&pbs[j]);
       pos = put_bits_count(&pbs[j]) >> 3;
       if (pos > size) {
           av_log(avctx, AV_LOG_ERROR, "bitstream written beyond buffer size\n");
           return -1;
       }
       memset(pbs[j].buf + pos, 0xff, size - pos);
    }

    return 0;
}

static inline int dv_write_pack(enum dv_pack_type pack_id, DVVideoContext *c,
                                uint8_t* buf)
{
    /*
     * Here's what SMPTE314M says about these two:
     *    (page 6) APTn, AP1n, AP2n, AP3n: These data shall be identical
     *             as track application IDs (APTn = 001, AP1n =
     *             001, AP2n = 001, AP3n = 001), if the source signal
     *             comes from a digital VCR. If the signal source is
     *             unknown, all bits for these data shall be set to 1.
     *    (page 12) STYPE: STYPE defines a signal type of video signal
     *                     00000b = 4:1:1 compression
     *                     00100b = 4:2:2 compression
     *                     XXXXXX = Reserved
     * Now, I've got two problems with these statements:
     *   1. it looks like APT == 111b should be a safe bet, but it isn't.
     *      It seems that for PAL as defined in IEC 61834 we have to set
     *      APT to 000 and for SMPTE314M to 001.
     *   2. It is not at all clear what STYPE is used for 4:2:0 PAL
     *      compression scheme (if any).
     */
    int apt   = (c->sys->pix_fmt == AV_PIX_FMT_YUV420P ? 0 : 1);
    int fs    = c->picture.top_field_first ? 0x00 : 0x40;

    uint8_t aspect = 0;
    if ((int)(av_q2d(c->avctx->sample_aspect_ratio) * c->avctx->width / c->avctx->height * 10) >= 17) /* 16:9 */
        aspect = 0x02;

    buf[0] = (uint8_t)pack_id;
    switch (pack_id) {
    case dv_header525: /* I can't imagine why these two weren't defined as real */
    case dv_header625: /* packs in SMPTE314M -- they definitely look like ones */
          buf[1] = 0xf8 |        /* reserved -- always 1 */
                   (apt & 0x07); /* APT: Track application ID */
          buf[2] = (0    << 7) | /* TF1: audio data is 0 - valid; 1 - invalid */
                   (0x0f << 3) | /* reserved -- always 1 */
                   (apt & 0x07); /* AP1: Audio application ID */
          buf[3] = (0    << 7) | /* TF2: video data is 0 - valid; 1 - invalid */
                   (0x0f << 3) | /* reserved -- always 1 */
                   (apt & 0x07); /* AP2: Video application ID */
          buf[4] = (0    << 7) | /* TF3: subcode(SSYB) is 0 - valid; 1 - invalid */
                   (0x0f << 3) | /* reserved -- always 1 */
                   (apt & 0x07); /* AP3: Subcode application ID */
          break;
    case dv_video_source:
          buf[1] = 0xff;      /* reserved -- always 1 */
          buf[2] = (1 << 7) | /* B/W: 0 - b/w, 1 - color */
                   (1 << 6) | /* following CLF is valid - 0, invalid - 1 */
                   (3 << 4) | /* CLF: color frames ID (see ITU-R BT.470-4) */
                   0xf;       /* reserved -- always 1 */
          buf[3] = (3 << 6) | /* reserved -- always 1 */
                   (c->sys->dsf << 5) | /*  system: 60fields/50fields */
                   c->sys->video_stype; /* signal type video compression */
          buf[4] = 0xff;      /* VISC: 0xff -- no information */
          break;
    case dv_video_control:
          buf[1] = (0 << 6) | /* Copy generation management (CGMS) 0 -- free */
                   0x3f;      /* reserved -- always 1 */
          buf[2] = 0xc8 |     /* reserved -- always b11001xxx */
                   aspect;
          buf[3] = (1 << 7) | /* frame/field flag 1 -- frame, 0 -- field */
                   fs       | /* first/second field flag 0 -- field 2, 1 -- field 1 */
                   (1 << 5) | /* frame change flag 0 -- same picture as before, 1 -- different */
                   (1 << 4) | /* 1 - interlaced, 0 - noninterlaced */
                   0xc;       /* reserved -- always b1100 */
          buf[4] = 0xff;      /* reserved -- always 1 */
          break;
    default:
          buf[1] = buf[2] = buf[3] = buf[4] = 0xff;
    }
    return 5;
}

#if CONFIG_DVVIDEO_ENCODER
static inline int dv_write_dif_id(enum dv_section_type t, uint8_t chan_num,
                                  uint8_t seq_num, uint8_t dif_num,
                                  uint8_t* buf)
{
    buf[0] = (uint8_t)t;       /* Section type */
    buf[1] = (seq_num  << 4) | /* DIF seq number 0-9 for 525/60; 0-11 for 625/50 */
             (chan_num << 3) | /* FSC: for 50Mb/s 0 - first channel; 1 - second */
             7;                /* reserved -- always 1 */
    buf[2] = dif_num;          /* DIF block number Video: 0-134, Audio: 0-8 */
    return 3;
}


static inline int dv_write_ssyb_id(uint8_t syb_num, uint8_t fr, uint8_t* buf)
{
    if (syb_num == 0 || syb_num == 6) {
        buf[0] = (fr << 7) | /* FR ID 1 - first half of each channel; 0 - second */
                 (0  << 4) | /* AP3 (Subcode application ID) */
                 0x0f;       /* reserved -- always 1 */
    }
    else if (syb_num == 11) {
        buf[0] = (fr << 7) | /* FR ID 1 - first half of each channel; 0 - second */
                 0x7f;       /* reserved -- always 1 */
    }
    else {
        buf[0] = (fr << 7) | /* FR ID 1 - first half of each channel; 0 - second */
                 (0  << 4) | /* APT (Track application ID) */
                 0x0f;       /* reserved -- always 1 */
    }
    buf[1] = 0xf0 |            /* reserved -- always 1 */
             (syb_num & 0x0f); /* SSYB number 0 - 11   */
    buf[2] = 0xff;             /* reserved -- always 1 */
    return 3;
}

static void dv_format_frame(DVVideoContext* c, uint8_t* buf)
{
    int chan, i, j, k;

    for (chan = 0; chan < c->sys->n_difchan; chan++) {
        for (i = 0; i < c->sys->difseg_size; i++) {
            memset(buf, 0xff, 80 * 6); /* first 6 DIF blocks are for control data */

            /* DV header: 1DIF */
            buf += dv_write_dif_id(dv_sect_header, chan, i, 0, buf);
            buf += dv_write_pack((c->sys->dsf ? dv_header625 : dv_header525), c, buf);
            buf += 72; /* unused bytes */

            /* DV subcode: 2DIFs */
            for (j = 0; j < 2; j++) {
                buf += dv_write_dif_id(dv_sect_subcode, chan, i, j, buf);
                for (k = 0; k < 6; k++)
                     buf += dv_write_ssyb_id(k, (i < c->sys->difseg_size/2), buf) + 5;
                buf += 29; /* unused bytes */
            }

            /* DV VAUX: 3DIFS */
            for (j = 0; j < 3; j++) {
                buf += dv_write_dif_id(dv_sect_vaux, chan, i, j, buf);
                buf += dv_write_pack(dv_video_source,  c, buf);
                buf += dv_write_pack(dv_video_control, c, buf);
                buf += 7*5;
                buf += dv_write_pack(dv_video_source,  c, buf);
                buf += dv_write_pack(dv_video_control, c, buf);
                buf += 4*5 + 2; /* unused bytes */
            }

            /* DV Audio/Video: 135 Video DIFs + 9 Audio DIFs */
            for (j = 0; j < 135; j++) {
                if (j%15 == 0) {
                    memset(buf, 0xff, 80);
                    buf += dv_write_dif_id(dv_sect_audio, chan, i, j/15, buf);
                    buf += 77; /* audio control & shuffled PCM audio */
                }
                buf += dv_write_dif_id(dv_sect_video, chan, i, j, buf);
                buf += 77; /* 1 video macroblock: 1 bytes control
                              4 * 14 bytes Y 8x8 data
                              10 bytes Cr 8x8 data
                              10 bytes Cb 8x8 data */
            }
        }
    }
}


static int dvvideo_encode_frame(AVCodecContext *c, AVPacket *pkt,
                                const AVFrame *frame, int *got_packet)
{
    DVVideoContext *s = c->priv_data;
    int ret;

    s->sys = avpriv_dv_codec_profile(c);
    if (!s->sys || ff_dv_init_dynamic_tables(s->sys))
        return -1;
    if ((ret = ff_alloc_packet2(c, pkt, s->sys->frame_size)) < 0)
        return ret;

    c->pix_fmt           = s->sys->pix_fmt;
    s->picture           = *frame;
    s->picture.key_frame = 1;
    s->picture.pict_type = AV_PICTURE_TYPE_I;

    s->buf = pkt->data;
    c->execute(c, dv_encode_video_segment, s->sys->work_chunks, NULL,
               dv_work_pool_size(s->sys), sizeof(DVwork_chunk));

    emms_c();

    dv_format_frame(s, pkt->data);

    pkt->flags |= AV_PKT_FLAG_KEY;
    *got_packet = 1;

    return 0;
}

AVCodec ff_dvvideo_encoder = {
    .name           = "dvvideo",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_DVVIDEO,
    .priv_data_size = sizeof(DVVideoContext),
    .init           = dvvideo_init_encoder,
    .encode2        = dvvideo_encode_frame,
    .capabilities   = CODEC_CAP_SLICE_THREADS,
    .pix_fmts       = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_YUV411P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE
    },
    .long_name      = NULL_IF_CONFIG_SMALL("DV (Digital Video)"),
};
#endif // CONFIG_DVVIDEO_ENCODER
