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
#define ALT_BITSTREAM_READER
#include "libavutil/pixdesc.h"
#include "avcodec.h"
#include "dsputil.h"
#include "get_bits.h"
#include "put_bits.h"
#include "simple_idct.h"
#include "dvdata.h"
#include "dv_tablegen.h"

//#undef NDEBUG
//#include <assert.h>

typedef struct DVVideoContext {
    const DVprofile *sys;
    AVFrame          picture;
    AVCodecContext  *avctx;
    uint8_t         *buf;

    uint8_t  dv_zigzag[2][64];

    void (*get_pixels)(DCTELEM *block, const uint8_t *pixels, int line_size);
    void (*fdct[2])(DCTELEM *block);
    void (*idct_put[2])(uint8_t *dest, int line_size, DCTELEM *block);
    me_cmp_func ildct_cmp;
} DVVideoContext;

#define TEX_VLC_BITS 9

/* XXX: also include quantization */
static RL_VLC_ELEM dv_rl_vlc[1184];

static inline int dv_work_pool_size(const DVprofile *d)
{
    int size = d->n_difchan*d->difseg_size*27;
    if (DV_PROFILE_IS_1080i50(d))
        size -= 3*27;
    if (DV_PROFILE_IS_720p50(d))
        size -= 4*27;
    return size;
}

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
              case PIX_FMT_YUV422P:
                   x = shuf3[m] + slot/3;
                   y = serpent1[slot] +
                       ((((seq + off[m]) % d->difseg_size)<<1) + chan)*3;
                   tbl[m] = (x<<1)|(y<<8);
                   break;
              case PIX_FMT_YUV420P:
                   x = shuf3[m] + slot/3;
                   y = serpent1[slot] +
                       ((seq + off[m]) % d->difseg_size)*3;
                   tbl[m] = (x<<1)|(y<<9);
                   break;
              case PIX_FMT_YUV411P:
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

static int dv_init_dynamic_tables(const DVprofile *d)
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
            iweight1 = &dv_iweight_720_y[0];
            iweight2 = &dv_iweight_720_c[0];
        } else {
            iweight1 = &dv_iweight_1080_y[0];
            iweight2 = &dv_iweight_1080_c[0];
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
            iweight1 = &dv_iweight_88[0];
            for (j = 0; j < 2; j++, iweight1 = &dv_iweight_248[0]) {
                for (s = 0; s < 22; s++) {
                    for (i = c = 0; c < 4; c++) {
                        for (; i < dv_quant_areas[c]; i++) {
                            *factor1   = iweight1[i] << (dv_quant_shifts[s][c] + 1);
                            *factor2++ = (*factor1++) << 1;
                        }
                    }
                }
            }
        }
    }

    return 0;
}

static av_cold int dvvideo_init(AVCodecContext *avctx)
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
        assert(dv_vlc.table_size == 1184);

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
            dv_rl_vlc[i].len   = len;
            dv_rl_vlc[i].level = level;
            dv_rl_vlc[i].run   = run;
        }
        free_vlc(&dv_vlc);

        dv_vlc_map_tableinit();
    }

    /* Generic DSP setup */
    dsputil_init(&dsp, avctx);
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
    if (!ff_dv_codec_profile(avctx)) {
        av_log(avctx, AV_LOG_ERROR, "Found no DV profile for %ix%i %s video\n",
               avctx->width, avctx->height, av_get_pix_fmt_name(avctx->pix_fmt));
        return -1;
    }

    return dvvideo_init(avctx);
}

typedef struct BlockInfo {
    const uint32_t *factor_table;
    const uint8_t *scan_table;
    uint8_t pos; /* position in block */
    void (*idct_put)(uint8_t *dest, int line_size, DCTELEM *block);
    uint8_t partial_bit_count;
    uint16_t partial_bit_buffer;
    int shift_offset;
} BlockInfo;

/* bit budget for AC only in 5 MBs */
static const int vs_total_ac_bits = (100 * 4 + 68*2) * 5;
/* see dv_88_areas and dv_248_areas for details */
static const int mb_area_start[5] = { 1, 6, 21, 43, 64 };

static inline int put_bits_left(PutBitContext* s)
{
    return (s->buf_end - s->buf) * 8 - put_bits_count(s);
}

/* decode ac coefficients */
static void dv_decode_ac(GetBitContext *gb, BlockInfo *mb, DCTELEM *block)
{
    int last_index = gb->size_in_bits;
    const uint8_t  *scan_table   = mb->scan_table;
    const uint32_t *factor_table = mb->factor_table;
    int pos               = mb->pos;
    int partial_bit_count = mb->partial_bit_count;
    int level, run, vlc_len, index;

    OPEN_READER(re, gb);
    UPDATE_CACHE(re, gb);

    /* if we must parse a partial vlc, we do it here */
    if (partial_bit_count > 0) {
        re_cache = ((unsigned)re_cache >> partial_bit_count) |
                   (mb->partial_bit_buffer << (sizeof(re_cache) * 8 - partial_bit_count));
        re_index -= partial_bit_count;
        mb->partial_bit_count = 0;
    }

    /* get the AC coefficients until last_index is reached */
    for (;;) {
        av_dlog(NULL, "%2d: bits=%04x index=%d\n", pos, SHOW_UBITS(re, gb, 16),
                re_index);
        /* our own optimized GET_RL_VLC */
        index   = NEG_USR32(re_cache, TEX_VLC_BITS);
        vlc_len = dv_rl_vlc[index].len;
        if (vlc_len < 0) {
            index = NEG_USR32((unsigned)re_cache << TEX_VLC_BITS, -vlc_len) + dv_rl_vlc[index].level;
            vlc_len = TEX_VLC_BITS - vlc_len;
        }
        level = dv_rl_vlc[index].level;
        run   = dv_rl_vlc[index].run;

        /* gotta check if we're still within gb boundaries */
        if (re_index + vlc_len > last_index) {
            /* should be < 16 bits otherwise a codeword could have been parsed */
            mb->partial_bit_count = last_index - re_index;
            mb->partial_bit_buffer = NEG_USR32(re_cache, mb->partial_bit_count);
            re_index = last_index;
            break;
        }
        re_index += vlc_len;

        av_dlog(NULL, "run=%d level=%d\n", run, level);
        pos += run;
        if (pos >= 64)
            break;

        level = (level * factor_table[pos] + (1 << (dv_iweight_bits - 1))) >> dv_iweight_bits;
        block[scan_table[pos]] = level;

        UPDATE_CACHE(re, gb);
    }
    CLOSE_READER(re, gb);
    mb->pos = pos;
}

static inline void bit_copy(PutBitContext *pb, GetBitContext *gb)
{
    int bits_left = get_bits_left(gb);
    while (bits_left >= MIN_CACHE_BITS) {
        put_bits(pb, MIN_CACHE_BITS, get_bits(gb, MIN_CACHE_BITS));
        bits_left -= MIN_CACHE_BITS;
    }
    if (bits_left > 0) {
        put_bits(pb, bits_left, get_bits(gb, bits_left));
    }
}

static inline void dv_calculate_mb_xy(DVVideoContext *s, DVwork_chunk *work_chunk, int m, int *mb_x, int *mb_y)
{
     *mb_x = work_chunk->mb_coordinates[m] & 0xff;
     *mb_y = work_chunk->mb_coordinates[m] >> 8;

     /* We work with 720p frames split in half. The odd half-frame (chan==2,3) is displaced :-( */
     if (s->sys->height == 720 && !(s->buf[1]&0x0C)) {
         *mb_y -= (*mb_y>17)?18:-72; /* shifting the Y coordinate down by 72/2 macro blocks */
     }
}

/* mb_x and mb_y are in units of 8 pixels */
static int dv_decode_video_segment(AVCodecContext *avctx, void *arg)
{
    DVVideoContext *s = avctx->priv_data;
    DVwork_chunk *work_chunk = arg;
    int quant, dc, dct_mode, class1, j;
    int mb_index, mb_x, mb_y, last_index;
    int y_stride, linesize;
    DCTELEM *block, *block1;
    int c_offset;
    uint8_t *y_ptr;
    const uint8_t *buf_ptr;
    PutBitContext pb, vs_pb;
    GetBitContext gb;
    BlockInfo mb_data[5 * DV_MAX_BPM], *mb, *mb1;
    LOCAL_ALIGNED_16(DCTELEM, sblock, [5*DV_MAX_BPM], [64]);
    LOCAL_ALIGNED_16(uint8_t, mb_bit_buffer, [80 + 4]); /* allow some slack */
    LOCAL_ALIGNED_16(uint8_t, vs_bit_buffer, [5 * 80 + 4]); /* allow some slack */
    const int log2_blocksize = 3-s->avctx->lowres;
    int is_field_mode[5];

    assert((((int)mb_bit_buffer) & 7) == 0);
    assert((((int)vs_bit_buffer) & 7) == 0);

    memset(sblock, 0, 5*DV_MAX_BPM*sizeof(*sblock));

    /* pass 1 : read DC and AC coefficients in blocks */
    buf_ptr = &s->buf[work_chunk->buf_offset*80];
    block1  = &sblock[0][0];
    mb1     = mb_data;
    init_put_bits(&vs_pb, vs_bit_buffer, 5 * 80);
    for (mb_index = 0; mb_index < 5; mb_index++, mb1 += s->sys->bpm, block1 += s->sys->bpm * 64) {
        /* skip header */
        quant = buf_ptr[3] & 0x0f;
        buf_ptr += 4;
        init_put_bits(&pb, mb_bit_buffer, 80);
        mb    = mb1;
        block = block1;
        is_field_mode[mb_index] = 0;
        for (j = 0; j < s->sys->bpm; j++) {
            last_index = s->sys->block_sizes[j];
            init_get_bits(&gb, buf_ptr, last_index);

            /* get the dc */
            dc       = get_sbits(&gb, 9);
            dct_mode = get_bits1(&gb);
            class1   = get_bits(&gb, 2);
            if (DV_PROFILE_IS_HD(s->sys)) {
                mb->idct_put     = s->idct_put[0];
                mb->scan_table   = s->dv_zigzag[0];
                mb->factor_table = &s->sys->idct_factor[(j >= 4)*4*16*64 + class1*16*64 + quant*64];
                is_field_mode[mb_index] |= !j && dct_mode;
            } else {
                mb->idct_put     = s->idct_put[dct_mode && log2_blocksize == 3];
                mb->scan_table   = s->dv_zigzag[dct_mode];
                mb->factor_table = &s->sys->idct_factor[(class1 == 3)*2*22*64 + dct_mode*22*64 +
                                                        (quant + dv_quant_offset[class1])*64];
            }
            dc = dc << 2;
            /* convert to unsigned because 128 is not added in the
               standard IDCT */
            dc += 1024;
            block[0] = dc;
            buf_ptr += last_index >> 3;
            mb->pos               = 0;
            mb->partial_bit_count = 0;

            av_dlog(avctx, "MB block: %d, %d ", mb_index, j);
            dv_decode_ac(&gb, mb, block);

            /* write the remaining bits  in a new buffer only if the
               block is finished */
            if (mb->pos >= 64)
                bit_copy(&pb, &gb);

            block += 64;
            mb++;
        }

        /* pass 2 : we can do it just after */
        av_dlog(avctx, "***pass 2 size=%d MB#=%d\n", put_bits_count(&pb), mb_index);
        block = block1;
        mb    = mb1;
        init_get_bits(&gb, mb_bit_buffer, put_bits_count(&pb));
        flush_put_bits(&pb);
        for (j = 0; j < s->sys->bpm; j++, block += 64, mb++) {
            if (mb->pos < 64 && get_bits_left(&gb) > 0) {
                dv_decode_ac(&gb, mb, block);
                /* if still not finished, no need to parse other blocks */
                if (mb->pos < 64)
                    break;
            }
        }
        /* all blocks are finished, so the extra bytes can be used at
           the video segment level */
        if (j >= s->sys->bpm)
            bit_copy(&vs_pb, &gb);
    }

    /* we need a pass other the whole video segment */
    av_dlog(avctx, "***pass 3 size=%d\n", put_bits_count(&vs_pb));
    block = &sblock[0][0];
    mb    = mb_data;
    init_get_bits(&gb, vs_bit_buffer, put_bits_count(&vs_pb));
    flush_put_bits(&vs_pb);
    for (mb_index = 0; mb_index < 5; mb_index++) {
        for (j = 0; j < s->sys->bpm; j++) {
            if (mb->pos < 64) {
                av_dlog(avctx, "start %d:%d\n", mb_index, j);
                dv_decode_ac(&gb, mb, block);
            }
            if (mb->pos >= 64 && mb->pos < 127)
                av_log(avctx, AV_LOG_ERROR, "AC EOB marker is absent pos=%d\n", mb->pos);
            block += 64;
            mb++;
        }
    }

    /* compute idct and place blocks */
    block = &sblock[0][0];
    mb    = mb_data;
    for (mb_index = 0; mb_index < 5; mb_index++) {
        dv_calculate_mb_xy(s, work_chunk, mb_index, &mb_x, &mb_y);

        /* idct_put'ting luminance */
        if ((s->sys->pix_fmt == PIX_FMT_YUV420P) ||
            (s->sys->pix_fmt == PIX_FMT_YUV411P && mb_x >= (704 / 8)) ||
            (s->sys->height >= 720 && mb_y != 134)) {
            y_stride = (s->picture.linesize[0] << ((!is_field_mode[mb_index]) * log2_blocksize));
        } else {
            y_stride = (2 << log2_blocksize);
        }
        y_ptr = s->picture.data[0] + ((mb_y * s->picture.linesize[0] + mb_x) << log2_blocksize);
        linesize = s->picture.linesize[0] << is_field_mode[mb_index];
        mb[0]    .idct_put(y_ptr                                   , linesize, block + 0*64);
        if (s->sys->video_stype == 4) { /* SD 422 */
            mb[2].idct_put(y_ptr + (1 << log2_blocksize)           , linesize, block + 2*64);
        } else {
            mb[1].idct_put(y_ptr + (1 << log2_blocksize)           , linesize, block + 1*64);
            mb[2].idct_put(y_ptr                         + y_stride, linesize, block + 2*64);
            mb[3].idct_put(y_ptr + (1 << log2_blocksize) + y_stride, linesize, block + 3*64);
        }
        mb += 4;
        block += 4*64;

        /* idct_put'ting chrominance */
        c_offset = (((mb_y >>  (s->sys->pix_fmt == PIX_FMT_YUV420P)) * s->picture.linesize[1] +
                     (mb_x >> ((s->sys->pix_fmt == PIX_FMT_YUV411P) ? 2 : 1))) << log2_blocksize);
        for (j = 2; j; j--) {
            uint8_t *c_ptr = s->picture.data[j] + c_offset;
            if (s->sys->pix_fmt == PIX_FMT_YUV411P && mb_x >= (704 / 8)) {
                  uint64_t aligned_pixels[64/8];
                  uint8_t *pixels = (uint8_t*)aligned_pixels;
                  uint8_t *c_ptr1, *ptr1;
                  int x, y;
                  mb->idct_put(pixels, 8, block);
                  for (y = 0; y < (1 << log2_blocksize); y++, c_ptr += s->picture.linesize[j], pixels += 8) {
                      ptr1   = pixels + (1 << (log2_blocksize - 1));
                      c_ptr1 = c_ptr + (s->picture.linesize[j] << log2_blocksize);
                      for (x = 0; x < (1 << (log2_blocksize - 1)); x++) {
                          c_ptr[x]  = pixels[x];
                          c_ptr1[x] = ptr1[x];
                      }
                  }
                  block += 64; mb++;
            } else {
                  y_stride = (mb_y == 134) ? (1 << log2_blocksize) :
                                             s->picture.linesize[j] << ((!is_field_mode[mb_index]) * log2_blocksize);
                  linesize = s->picture.linesize[j] << is_field_mode[mb_index];
                  (mb++)->    idct_put(c_ptr           , linesize, block); block += 64;
                  if (s->sys->bpm == 8) {
                      (mb++)->idct_put(c_ptr + y_stride, linesize, block); block += 64;
                  }
            }
        }
    }
    return 0;
}

#if CONFIG_SMALL
/* Converts run and level (where level != 0) pair into vlc, returning bit size */
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
    DCTELEM  mb[64];
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
            return (ps > is);
        }
    }

    return 0;
}

static av_always_inline int dv_init_enc_block(EncBlockInfo* bi, uint8_t *data, int linesize, DVVideoContext *s, int bias)
{
    const int *weight;
    const uint8_t* zigzag_scan;
    LOCAL_ALIGNED_16(DCTELEM, blk, [64]);
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

    assert((((int)blk) & 15) == 0);

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
              /* weigh it and and shift down into range, adding for rounding */
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
                if (b->area_q[a] != dv_quant_shifts[qnos[i] + dv_quant_offset[b->cno]][a]) {
                    b->bit_size[a] = 1; // 4 areas 4 bits for EOB :)
                    b->area_q[a]++;
                    prev = b->prev[a];
                    assert(b->next[prev] >= mb_area_start[a+1] || b->mb[prev]);
                    for (k = b->next[prev] ; k < mb_area_start[a+1]; k = b->next[k]) {
                       b->mb[k] >>= 1;
                       if (b->mb[k]) {
                           b->bit_size[a] += dv_rl2vlc_size(k - prev - 1, b->mb[k]);
                           prev = k;
                       } else {
                           if (b->next[k] >= mb_area_start[a+1] && b->next[k]<64){
                                for (a2 = a + 1; b->next[k] >= mb_area_start[a2+1]; a2++)
                                    b->prev[a2] = prev;
                                assert(a2 < 4);
                                assert(b->mb[b->next[k]]);
                                b->bit_size[a2] += dv_rl2vlc_size(b->next[k] - prev - 1, b->mb[b->next[k]])
                                                  -dv_rl2vlc_size(b->next[k] -    k - 1, b->mb[b->next[k]]);
                                assert(b->prev[a2] == k && (a2 + 1 >= 4 || b->prev[a2+1] != k));
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
    LOCAL_ALIGNED_8(uint8_t, scratch, [64]);
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
        if ((s->sys->pix_fmt == PIX_FMT_YUV420P) ||
            (s->sys->pix_fmt == PIX_FMT_YUV411P && mb_x >= (704 / 8)) ||
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
        c_offset = (((mb_y >>  (s->sys->pix_fmt == PIX_FMT_YUV420P)) * s->picture.linesize[1] +
                     (mb_x >> ((s->sys->pix_fmt == PIX_FMT_YUV411P) ? 2 : 1))) << 3);
        for (j = 2; j; j--) {
            uint8_t *c_ptr = s->picture.data[j] + c_offset;
            linesize = s->picture.linesize[j];
            y_stride = (mb_y == 134) ? 8 : (s->picture.linesize[j] << 3);
            if (s->sys->pix_fmt == PIX_FMT_YUV411P && mb_x >= (704 / 8)) {
                uint8_t* d;
                uint8_t* b = scratch;
                for (i = 0; i < 8; i++) {
                    d = c_ptr + (linesize << 3);
                    b[0] = c_ptr[0]; b[1] = c_ptr[1]; b[2] = c_ptr[2]; b[3] = c_ptr[3];
                    b[4] =     d[0]; b[5] =     d[1]; b[6] =     d[2]; b[7] =     d[3];
                    c_ptr += linesize;
                    b += 8;
                }
                c_ptr = scratch;
                linesize = 8;
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

#if CONFIG_DVVIDEO_DECODER
/* NOTE: exactly one frame must be given (120000 bytes for NTSC,
   144000 bytes for PAL - or twice those for 50Mbps) */
static int dvvideo_decode_frame(AVCodecContext *avctx,
                                 void *data, int *data_size,
                                 AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    DVVideoContext *s = avctx->priv_data;
    const uint8_t* vsc_pack;
    int apt, is16_9;

    s->sys = ff_dv_frame_profile(s->sys, buf, buf_size);
    if (!s->sys || buf_size < s->sys->frame_size || dv_init_dynamic_tables(s->sys)) {
        av_log(avctx, AV_LOG_ERROR, "could not find dv frame profile\n");
        return -1; /* NOTE: we only accept several full frames */
    }

    if (s->picture.data[0])
        avctx->release_buffer(avctx, &s->picture);

    avcodec_get_frame_defaults(&s->picture);
    s->picture.reference = 0;
    s->picture.key_frame = 1;
    s->picture.pict_type = AV_PICTURE_TYPE_I;
    avctx->pix_fmt   = s->sys->pix_fmt;
    avctx->time_base = s->sys->time_base;
    avcodec_set_dimensions(avctx, s->sys->width, s->sys->height);
    if (avctx->get_buffer(avctx, &s->picture) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }
    s->picture.interlaced_frame = 1;
    s->picture.top_field_first  = 0;

    s->buf = buf;
    avctx->execute(avctx, dv_decode_video_segment, s->sys->work_chunks, NULL,
                   dv_work_pool_size(s->sys), sizeof(DVwork_chunk));

    emms_c();

    /* return image */
    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = s->picture;

    /* Determine the codec's sample_aspect ratio from the packet */
    vsc_pack = buf + 80*5 + 48 + 5;
    if ( *vsc_pack == dv_video_control ) {
        apt = buf[4] & 0x07;
        is16_9 = (vsc_pack && ((vsc_pack[2] & 0x07) == 0x02 || (!apt && (vsc_pack[2] & 0x07) == 0x07)));
        avctx->sample_aspect_ratio = s->sys->sar[is16_9];
    }

    return s->sys->frame_size;
}
#endif /* CONFIG_DVVIDEO_DECODER */


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
    int apt   = (c->sys->pix_fmt == PIX_FMT_YUV420P ? 0 : 1);

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
                   (1 << 6) | /* first/second field flag 0 -- field 2, 1 -- field 1 */
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


static int dvvideo_encode_frame(AVCodecContext *c, uint8_t *buf, int buf_size,
                                void *data)
{
    DVVideoContext *s = c->priv_data;

    s->sys = ff_dv_codec_profile(c);
    if (!s->sys || buf_size < s->sys->frame_size || dv_init_dynamic_tables(s->sys))
        return -1;

    c->pix_fmt           = s->sys->pix_fmt;
    s->picture           = *((AVFrame *)data);
    s->picture.key_frame = 1;
    s->picture.pict_type = AV_PICTURE_TYPE_I;

    s->buf = buf;
    c->execute(c, dv_encode_video_segment, s->sys->work_chunks, NULL,
               dv_work_pool_size(s->sys), sizeof(DVwork_chunk));

    emms_c();

    dv_format_frame(s, buf);

    return s->sys->frame_size;
}
#endif

static int dvvideo_close(AVCodecContext *c)
{
    DVVideoContext *s = c->priv_data;

    if (s->picture.data[0])
        c->release_buffer(c, &s->picture);

    return 0;
}


#if CONFIG_DVVIDEO_ENCODER
AVCodec ff_dvvideo_encoder = {
    "dvvideo",
    AVMEDIA_TYPE_VIDEO,
    CODEC_ID_DVVIDEO,
    sizeof(DVVideoContext),
    dvvideo_init_encoder,
    dvvideo_encode_frame,
    .capabilities = CODEC_CAP_SLICE_THREADS,
    .pix_fmts  = (const enum PixelFormat[]) {PIX_FMT_YUV411P, PIX_FMT_YUV422P, PIX_FMT_YUV420P, PIX_FMT_NONE},
    .long_name = NULL_IF_CONFIG_SMALL("DV (Digital Video)"),
};
#endif // CONFIG_DVVIDEO_ENCODER

#if CONFIG_DVVIDEO_DECODER
AVCodec ff_dvvideo_decoder = {
    "dvvideo",
    AVMEDIA_TYPE_VIDEO,
    CODEC_ID_DVVIDEO,
    sizeof(DVVideoContext),
    dvvideo_init,
    NULL,
    dvvideo_close,
    dvvideo_decode_frame,
    CODEC_CAP_DR1 | CODEC_CAP_SLICE_THREADS,
    NULL,
    .max_lowres = 3,
    .long_name = NULL_IF_CONFIG_SMALL("DV (Digital Video)"),
};
#endif
