/*
 * DV encoder
 * Copyright (c) 2003 Roman Shaposhnik
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
 *
 * quant_deadzone code and fixes sponsored by NOA GmbH
 */

/**
 * @file
 * DV encoder
 */

#include "config.h"

#include "libavutil/attributes.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "avcodec.h"
#include "dv.h"
#include "dv_profile_internal.h"
#include "dv_tablegen.h"
#include "fdctdsp.h"
#include "internal.h"
#include "mathops.h"
#include "me_cmp.h"
#include "pixblockdsp.h"
#include "put_bits.h"

static av_cold int dvvideo_encode_init(AVCodecContext *avctx)
{
    DVVideoContext *s = avctx->priv_data;
    FDCTDSPContext fdsp;
    MECmpContext mecc;
    PixblockDSPContext pdsp;
    int ret;

    s->sys = av_dv_codec_profile2(avctx->width, avctx->height, avctx->pix_fmt, avctx->time_base);
    if (!s->sys) {
        av_log(avctx, AV_LOG_ERROR, "Found no DV profile for %ix%i %s video. "
                                    "Valid DV profiles are:\n",
               avctx->width, avctx->height, av_get_pix_fmt_name(avctx->pix_fmt));
        ff_dv_print_profiles(avctx, AV_LOG_ERROR);
        return AVERROR(EINVAL);
    }

    ret = ff_dv_init_dynamic_tables(s, s->sys);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error initializing work tables.\n");
        return ret;
    }

    dv_vlc_map_tableinit();

    memset(&fdsp,0, sizeof(fdsp));
    memset(&mecc,0, sizeof(mecc));
    memset(&pdsp,0, sizeof(pdsp));
    ff_fdctdsp_init(&fdsp, avctx);
    ff_me_cmp_init(&mecc, avctx);
    ff_pixblockdsp_init(&pdsp, avctx);
    ff_set_cmp(&mecc, mecc.ildct_cmp, avctx->ildct_cmp);

    s->get_pixels = pdsp.get_pixels;
    s->ildct_cmp  = mecc.ildct_cmp[5];

    s->fdct[0]    = fdsp.fdct;
    s->fdct[1]    = fdsp.fdct248;

    return ff_dvvideo_init(avctx);
}

/* bit budget for AC only in 5 MBs */
static const int vs_total_ac_bits_hd = (68 * 6 + 52*2) * 5;
static const int vs_total_ac_bits = (100 * 4 + 68 * 2) * 5;
static const int mb_area_start[5] = { 1, 6, 21, 43, 64 };

#if CONFIG_SMALL
/* Convert run and level (where level != 0) pair into VLC, returning bit size */
static av_always_inline int dv_rl2vlc(int run, int level, int sign,
                                      uint32_t *vlc)
{
    int size;
    if (run < DV_VLC_MAP_RUN_SIZE && level < DV_VLC_MAP_LEV_SIZE) {
        *vlc = dv_vlc_map[run][level].vlc | sign;
        size = dv_vlc_map[run][level].size;
    } else {
        if (level < DV_VLC_MAP_LEV_SIZE) {
            *vlc = dv_vlc_map[0][level].vlc | sign;
            size = dv_vlc_map[0][level].size;
        } else {
            *vlc = 0xfe00 | (level << 1) | sign;
            size = 16;
        }
        if (run) {
            *vlc |= ((run < 16) ? dv_vlc_map[run - 1][0].vlc :
                     (0x1f80 | (run - 1))) << size;
            size +=  (run < 16) ? dv_vlc_map[run - 1][0].size : 13;
        }
    }

    return size;
}

static av_always_inline int dv_rl2vlc_size(int run, int level)
{
    int size;

    if (run < DV_VLC_MAP_RUN_SIZE && level < DV_VLC_MAP_LEV_SIZE) {
        size = dv_vlc_map[run][level].size;
    } else {
        size = (level < DV_VLC_MAP_LEV_SIZE) ? dv_vlc_map[0][level].size : 16;
        if (run)
            size += (run < 16) ? dv_vlc_map[run - 1][0].size : 13;
    }
    return size;
}
#else
static av_always_inline int dv_rl2vlc(int run, int l, int sign, uint32_t *vlc)
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
    /* used by DV100 only: a copy of the weighted and classified but
       not-yet-quantized AC coefficients. This is necessary for
       re-quantizing at different steps. */
    int16_t  save[64];
    int      min_qlevel; /* DV100 only: minimum qlevel (for AC coefficients >255) */
} EncBlockInfo;

static av_always_inline PutBitContext *dv_encode_ac(EncBlockInfo *bi,
                                                    PutBitContext *pb_pool,
                                                    PutBitContext *pb_end)
{
    int prev, bits_left;
    PutBitContext *pb = pb_pool;
    int size          = bi->partial_bit_count;
    uint32_t vlc      = bi->partial_bit_buffer;

    bi->partial_bit_count  =
    bi->partial_bit_buffer = 0;
    for (;;) {
        /* Find suitable storage space */
        for (; size > (bits_left = put_bits_left(pb)); pb++) {
            if (bits_left) {
                size -= bits_left;
                put_bits(pb, bits_left, vlc >> size);
                vlc = av_mod_uintp2(vlc, size);
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
        if (bi->cur_ac < 64) {
            size = dv_rl2vlc(bi->cur_ac - prev - 1, bi->mb[bi->cur_ac],
                             bi->sign[bi->cur_ac], &vlc);
        } else {
            size = 4;
            vlc  = 6; /* End Of Block stamp */
        }
    }
    return pb;
}

static av_always_inline int dv_guess_dct_mode(DVVideoContext *s, uint8_t *data,
                                              ptrdiff_t linesize)
{
    if (s->avctx->flags & AV_CODEC_FLAG_INTERLACED_DCT) {
        int ps = s->ildct_cmp(NULL, data, NULL, linesize, 8) - 400;
        if (ps > 0) {
            int is = s->ildct_cmp(NULL, data,            NULL, linesize << 1, 4) +
                     s->ildct_cmp(NULL, data + linesize, NULL, linesize << 1, 4);
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
    131072, 262144, 257107, 257107, 242189, 242189, 242189, 242189,
    237536, 237536, 229376, 229376, 200636, 200636, 224973, 224973,
    223754, 223754, 235923, 235923, 229376, 229376, 217965, 217965,
    211916, 211916, 196781, 196781, 185364, 185364, 206433, 206433,
    211916, 211916, 222935, 222935, 200636, 200636, 205964, 205964,
    200704, 200704, 180568, 180568, 175557, 175557, 195068, 195068,
    185364, 185364, 188995, 188995, 174606, 174606, 175557, 175557,
    170627, 170627, 153560, 153560, 165371, 165371, 144651, 144651,
};

/* setting this to 1 results in a faster codec but
 * somewhat lower image quality */
#define DV100_SACRIFICE_QUALITY_FOR_SPEED 1
#define DV100_ENABLE_FINER 1

/* pack combination of QNO and CNO into a single 8-bit value */
#define DV100_MAKE_QLEVEL(qno,cno) ((qno<<2) | (cno))
#define DV100_QLEVEL_QNO(qlevel) (qlevel>>2)
#define DV100_QLEVEL_CNO(qlevel) (qlevel&0x3)

#define DV100_NUM_QLEVELS 31

/* The quantization step is determined by a combination of QNO and
   CNO. We refer to these combinations as "qlevels" (this term is our
   own, it's not mentioned in the spec). We use CNO, a multiplier on
   the quantization step, to "fill in the gaps" between quantization
   steps associated with successive values of QNO. e.g. there is no
   QNO for a quantization step of 10, but we can use QNO=5 CNO=1 to
   get the same result. The table below encodes combinations of QNO
   and CNO in order of increasing quantization coarseness. */
static const uint8_t dv100_qlevels[DV100_NUM_QLEVELS] = {
    DV100_MAKE_QLEVEL( 1,0), //  1*1= 1
    DV100_MAKE_QLEVEL( 1,0), //  1*1= 1
    DV100_MAKE_QLEVEL( 2,0), //  2*1= 2
    DV100_MAKE_QLEVEL( 3,0), //  3*1= 3
    DV100_MAKE_QLEVEL( 4,0), //  4*1= 4
    DV100_MAKE_QLEVEL( 5,0), //  5*1= 5
    DV100_MAKE_QLEVEL( 6,0), //  6*1= 6
    DV100_MAKE_QLEVEL( 7,0), //  7*1= 7
    DV100_MAKE_QLEVEL( 8,0), //  8*1= 8
    DV100_MAKE_QLEVEL( 5,1), //  5*2=10
    DV100_MAKE_QLEVEL( 6,1), //  6*2=12
    DV100_MAKE_QLEVEL( 7,1), //  7*2=14
    DV100_MAKE_QLEVEL( 9,0), // 16*1=16
    DV100_MAKE_QLEVEL(10,0), // 18*1=18
    DV100_MAKE_QLEVEL(11,0), // 20*1=20
    DV100_MAKE_QLEVEL(12,0), // 22*1=22
    DV100_MAKE_QLEVEL(13,0), // 24*1=24
    DV100_MAKE_QLEVEL(14,0), // 28*1=28
    DV100_MAKE_QLEVEL( 9,1), // 16*2=32
    DV100_MAKE_QLEVEL(10,1), // 18*2=36
    DV100_MAKE_QLEVEL(11,1), // 20*2=40
    DV100_MAKE_QLEVEL(12,1), // 22*2=44
    DV100_MAKE_QLEVEL(13,1), // 24*2=48
    DV100_MAKE_QLEVEL(15,0), // 52*1=52
    DV100_MAKE_QLEVEL(14,1), // 28*2=56
    DV100_MAKE_QLEVEL( 9,2), // 16*4=64
    DV100_MAKE_QLEVEL(10,2), // 18*4=72
    DV100_MAKE_QLEVEL(11,2), // 20*4=80
    DV100_MAKE_QLEVEL(12,2), // 22*4=88
    DV100_MAKE_QLEVEL(13,2), // 24*4=96
    // ...
    DV100_MAKE_QLEVEL(15,3), // 52*8=416
};

static const int dv100_min_bias = 0;
static const int dv100_chroma_bias = 0;
static const int dv100_starting_qno = 1;

#if DV100_SACRIFICE_QUALITY_FOR_SPEED
static const int dv100_qlevel_inc = 4;
#else
static const int dv100_qlevel_inc = 1;
#endif

// 1/qstep, shifted up by 16 bits
static const int dv100_qstep_bits = 16;
static const int dv100_qstep_inv[16] = {
        65536,  65536,  32768,  21845,  16384,  13107,  10923,  9362,  8192,  4096,  3641,  3277,  2979,  2731,  2341,  1260,
};

/* DV100 weights are pre-zigzagged, inverted and multiplied by 2^(dv100_weight_shift)
   (in DV100 the AC components are divided by the spec weights) */
static const int dv100_weight_shift = 16;
static const int dv_weight_1080[2][64] = {
    { 8192, 65536, 65536, 61681, 61681, 61681, 58254, 58254,
      58254, 58254, 58254, 58254, 55188, 58254, 58254, 55188,
      55188, 55188, 55188, 55188, 55188, 24966, 27594, 26214,
      26214, 26214, 27594, 24966, 23831, 24385, 25575, 25575,
      25575, 25575, 24385, 23831, 23302, 23302, 24966, 24966,
      24966, 23302, 23302, 21845, 22795, 24385, 24385, 22795,
      21845, 21400, 21845, 23831, 21845, 21400, 10382, 10700,
      10700, 10382, 10082, 9620, 10082, 9039, 9039, 8525, },
    { 8192, 65536, 65536, 61681, 61681, 61681, 41943, 41943,
      41943, 41943, 40330, 41943, 40330, 41943, 40330, 40330,
      40330, 38836, 38836, 40330, 40330, 24966, 27594, 26214,
      26214, 26214, 27594, 24966, 23831, 24385, 25575, 25575,
      25575, 25575, 24385, 23831, 11523, 11523, 12483, 12483,
      12483, 11523, 11523, 10923, 11275, 12193, 12193, 11275,
      10923, 5323, 5490, 5924, 5490, 5323, 5165, 5323,
      5323, 5165, 5017, 4788, 5017, 4520, 4520, 4263, }
};

static const int dv_weight_720[2][64] = {
    { 8192, 65536, 65536, 61681, 61681, 61681, 58254, 58254,
      58254, 58254, 58254, 58254, 55188, 58254, 58254, 55188,
      55188, 55188, 55188, 55188, 55188, 24966, 27594, 26214,
      26214, 26214, 27594, 24966, 23831, 24385, 25575, 25575,
      25575, 25575, 24385, 23831, 15420, 15420, 16644, 16644,
      16644, 15420, 15420, 10923, 11398, 12193, 12193, 11398,
      10923, 10700, 10923, 11916, 10923, 10700, 5191, 5350,
      5350, 5191, 5041, 4810, 5041, 4520, 4520, 4263, },
    { 8192, 43691, 43691, 40330, 40330, 40330, 29127, 29127,
      29127, 29127, 29127, 29127, 27594, 29127, 29127, 27594,
      27594, 27594, 27594, 27594, 27594, 12483, 13797, 13107,
      13107, 13107, 13797, 12483, 11916, 12193, 12788, 12788,
      12788, 12788, 12193, 11916, 5761, 5761, 6242, 6242,
      6242, 5761, 5761, 5461, 5638, 5461, 6096, 5638,
      5461, 2661, 2745, 2962, 2745, 2661, 2583, 2661,
      2661, 2583, 2509, 2394, 2509, 2260, 2260, 2131, }
};

static av_always_inline int dv_set_class_number_sd(DVVideoContext *s,
                                                   int16_t *blk, EncBlockInfo *bi,
                                                   const uint8_t *zigzag_scan,
                                                   const int *weight, int bias)
{
    int i, area;
    /* We offer two different methods for class number assignment: the
     * method suggested in SMPTE 314M Table 22, and an improved
     * method. The SMPTE method is very conservative; it assigns class
     * 3 (i.e. severe quantization) to any block where the largest AC
     * component is greater than 36. FFmpeg's DV encoder tracks AC bit
     * consumption precisely, so there is no need to bias most blocks
     * towards strongly lossy compression. Instead, we assign class 2
     * to most blocks, and use class 3 only when strictly necessary
     * (for blocks whose largest AC component exceeds 255). */

#if 0 /* SMPTE spec method */
    static const int classes[] = { 12, 24, 36, 0xffff };
#else /* improved FFmpeg method */
    static const int classes[] = { -1, -1, 255, 0xffff };
#endif
    int max  = classes[0];
    int prev = 0;
    const unsigned deadzone = s->quant_deadzone;
    const unsigned threshold = 2 * deadzone;

    bi->mb[0] = blk[0];

    for (area = 0; area < 4; area++) {
        bi->prev[area]     = prev;
        bi->bit_size[area] = 1; // 4 areas 4 bits for EOB :)
        for (i = mb_area_start[area]; i < mb_area_start[area + 1]; i++) {
            int level = blk[zigzag_scan[i]];

            if (level + deadzone > threshold) {
                bi->sign[i] = (level >> 31) & 1;
                /* Weight it and shift down into range, adding for rounding.
                 * The extra division by a factor of 2^4 reverses the 8x
                 * expansion of the DCT AND the 2x doubling of the weights. */
                level     = (FFABS(level) * weight[i] + (1 << (dv_weight_bits + 3))) >>
                            (dv_weight_bits + 4);
                if (!level)
                    continue;
                bi->mb[i] = level;
                if (level > max)
                    max = level;
                bi->bit_size[area] += dv_rl2vlc_size(i - prev - 1, level);
                bi->next[prev]      = i;
                prev                = i;
            }
        }
    }
    bi->next[prev] = i;
    for (bi->cno = 0; max > classes[bi->cno]; bi->cno++)
        ;

    bi->cno += bias;

    if (bi->cno >= 3) {
        bi->cno = 3;
        prev    = 0;
        i       = bi->next[prev];
        for (area = 0; area < 4; area++) {
            bi->prev[area]     = prev;
            bi->bit_size[area] = 1; // 4 areas 4 bits for EOB :)
            for (; i < mb_area_start[area + 1]; i = bi->next[i]) {
                bi->mb[i] >>= 1;

                if (bi->mb[i]) {
                    bi->bit_size[area] += dv_rl2vlc_size(i - prev - 1, bi->mb[i]);
                    bi->next[prev]      = i;
                    prev                = i;
                }
            }
        }
        bi->next[prev] = i;
    }

    return bi->bit_size[0] + bi->bit_size[1] +
           bi->bit_size[2] + bi->bit_size[3];
}

/* this function just copies the DCT coefficients and performs
   the initial (non-)quantization. */
static inline void dv_set_class_number_hd(DVVideoContext *s,
                                          int16_t *blk, EncBlockInfo *bi,
                                          const uint8_t *zigzag_scan,
                                          const int *weight, int bias)
{
    int i, max = 0;

    /* the first quantization (none at all) */
    bi->area_q[0] = 1;

    /* weigh AC components and store to save[] */
    /* (i=0 is the DC component; we only include it to make the
       number of loop iterations even, for future possible SIMD optimization) */
    for (i = 0; i < 64; i += 2) {
        int level0, level1;

        /* get the AC component (in zig-zag order) */
        level0 = blk[zigzag_scan[i+0]];
        level1 = blk[zigzag_scan[i+1]];

        /* extract sign and make it the lowest bit */
        bi->sign[i+0] = (level0>>31)&1;
        bi->sign[i+1] = (level1>>31)&1;

        /* take absolute value of the level */
        level0 = FFABS(level0);
        level1 = FFABS(level1);

        /* weigh it */
        level0 = (level0*weight[i+0] + 4096 + (1<<17)) >> 18;
        level1 = (level1*weight[i+1] + 4096 + (1<<17)) >> 18;

        /* save unquantized value */
        bi->save[i+0] = level0;
        bi->save[i+1] = level1;

         /* find max component */
        if (bi->save[i+0] > max)
            max = bi->save[i+0];
        if (bi->save[i+1] > max)
            max = bi->save[i+1];
    }

    /* copy DC component */
    bi->mb[0] = blk[0];

    /* the EOB code is 4 bits */
    bi->bit_size[0] = 4;
    bi->bit_size[1] = bi->bit_size[2] = bi->bit_size[3] = 0;

    /* ensure that no AC coefficients are cut off */
    bi->min_qlevel = ((max+256) >> 8);

    bi->area_q[0] = 25; /* set to an "impossible" value */
    bi->cno = 0;
}

static av_always_inline int dv_init_enc_block(EncBlockInfo* bi, uint8_t *data, int linesize,
                                              DVVideoContext *s, int chroma)
{
    LOCAL_ALIGNED_16(int16_t, blk, [64]);

    bi->area_q[0] = bi->area_q[1] = bi->area_q[2] = bi->area_q[3] = 0;
    bi->partial_bit_count = 0;
    bi->partial_bit_buffer = 0;
    bi->cur_ac = 0;

    if (data) {
        if (DV_PROFILE_IS_HD(s->sys)) {
            s->get_pixels(blk, data, linesize << bi->dct_mode);
            s->fdct[0](blk);
        } else {
            bi->dct_mode = dv_guess_dct_mode(s, data, linesize);
            s->get_pixels(blk, data, linesize);
            s->fdct[bi->dct_mode](blk);
        }
    } else {
        /* We rely on the fact that encoding all zeros leads to an immediate EOB,
           which is precisely what the spec calls for in the "dummy" blocks. */
        memset(blk, 0, 64*sizeof(*blk));
        bi->dct_mode = 0;
    }

    if (DV_PROFILE_IS_HD(s->sys)) {
        const int *weights;
        if (s->sys->height == 1080) {
            weights = dv_weight_1080[chroma];
        } else { /* 720p */
            weights = dv_weight_720[chroma];
        }
        dv_set_class_number_hd(s, blk, bi,
                               ff_zigzag_direct,
                               weights,
                               dv100_min_bias+chroma*dv100_chroma_bias);
    } else {
        dv_set_class_number_sd(s, blk, bi,
                               bi->dct_mode ? ff_dv_zigzag248_direct : ff_zigzag_direct,
                               bi->dct_mode ? dv_weight_248 : dv_weight_88,
                               chroma);
    }

    return bi->bit_size[0] + bi->bit_size[1] + bi->bit_size[2] + bi->bit_size[3];
}

/* DV100 quantize
   Perform quantization by divinding the AC component by the qstep.
   As an optimization we use a fixed-point integer multiply instead
   of a divide. */
static av_always_inline int dv100_quantize(int level, int qsinv)
{
    /* this code is equivalent to */
    /* return (level + qs/2) / qs; */

    return (level * qsinv + 1024 + (1<<(dv100_qstep_bits-1))) >> dv100_qstep_bits;

    /* the extra +1024 is needed to make the rounding come out right. */

    /* I (DJM) have verified that the results are exactly the same as
       division for level 0-2048 at all QNOs. */
}

static int dv100_actual_quantize(EncBlockInfo *b, int qlevel)
{
    int prev, k, qsinv;

    int qno = DV100_QLEVEL_QNO(dv100_qlevels[qlevel]);
    int cno = DV100_QLEVEL_CNO(dv100_qlevels[qlevel]);

    if (b->area_q[0] == qno && b->cno == cno)
        return b->bit_size[0];

    qsinv = dv100_qstep_inv[qno];

    /* record the new qstep */
    b->area_q[0] = qno;
    b->cno = cno;

    /* reset encoded size (EOB = 4 bits) */
    b->bit_size[0] = 4;

    /* visit nonzero components and quantize */
    prev = 0;
    for (k = 1; k < 64; k++) {
        /* quantize */
        int ac = dv100_quantize(b->save[k], qsinv) >> cno;
        if (ac) {
            if (ac > 255)
                ac = 255;
            b->mb[k] = ac;
            b->bit_size[0] += dv_rl2vlc_size(k - prev - 1, ac);
            b->next[prev] = k;
            prev = k;
        }
    }
    b->next[prev] = k;

    return b->bit_size[0];
}

static inline void dv_guess_qnos_hd(EncBlockInfo *blks, int *qnos)
{
    EncBlockInfo *b;
    int min_qlevel[5];
    int qlevels[5];
    int size[5];
    int i, j;
    /* cache block sizes at hypothetical qlevels */
    uint16_t size_cache[5*8][DV100_NUM_QLEVELS] = {{0}};

    /* get minimum qlevels */
    for (i = 0; i < 5; i++) {
        min_qlevel[i] = 1;
        for (j = 0; j < 8; j++) {
            if (blks[8*i+j].min_qlevel > min_qlevel[i])
                min_qlevel[i] = blks[8*i+j].min_qlevel;
        }
    }

    /* initialize sizes */
    for (i = 0; i < 5; i++) {
        qlevels[i] = dv100_starting_qno;
        if (qlevels[i] < min_qlevel[i])
            qlevels[i] = min_qlevel[i];

        qnos[i] = DV100_QLEVEL_QNO(dv100_qlevels[qlevels[i]]);
        size[i] = 0;
        for (j = 0; j < 8; j++) {
            size_cache[8*i+j][qlevels[i]] = dv100_actual_quantize(&blks[8*i+j], qlevels[i]);
            size[i] += size_cache[8*i+j][qlevels[i]];
        }
    }

    /* must we go coarser? */
    if (size[0]+size[1]+size[2]+size[3]+size[4] > vs_total_ac_bits_hd) {
        int largest = size[0] % 5; /* 'random' number */
        int qlevels_done = 0;

        do {
            /* find the macroblock with the lowest qlevel */
            for (i = 0; i < 5; i++) {
                if (qlevels[i] < qlevels[largest])
                    largest = i;
            }

            i = largest;
            /* ensure that we don't enter infinite loop */
            largest = (largest+1) % 5;

            /* quantize a little bit more */
            qlevels[i] += dv100_qlevel_inc;
            if (qlevels[i] > DV100_NUM_QLEVELS-1) {
                qlevels[i] = DV100_NUM_QLEVELS-1;
                qlevels_done++;
            }

            qnos[i] = DV100_QLEVEL_QNO(dv100_qlevels[qlevels[i]]);
            size[i] = 0;

            /* for each block */
            b = &blks[8*i];
            for (j = 0; j < 8; j++, b++) {
                /* accumulate block size into macroblock */
                if(size_cache[8*i+j][qlevels[i]] == 0) {
                    /* it is safe to use actual_quantize() here because we only go from finer to coarser,
                       and it saves the final actual_quantize() down below */
                    size_cache[8*i+j][qlevels[i]] = dv100_actual_quantize(b, qlevels[i]);
                }
                size[i] += size_cache[8*i+j][qlevels[i]];
            } /* for each block */

        } while (vs_total_ac_bits_hd < size[0] + size[1] + size[2] + size[3] + size[4] && qlevels_done < 5);

        // can we go finer?
    } else if (DV100_ENABLE_FINER &&
               size[0]+size[1]+size[2]+size[3]+size[4] < vs_total_ac_bits_hd) {
        int save_qlevel;
        int largest = size[0] % 5; /* 'random' number */

        while (qlevels[0] > min_qlevel[0] ||
               qlevels[1] > min_qlevel[1] ||
               qlevels[2] > min_qlevel[2] ||
               qlevels[3] > min_qlevel[3] ||
               qlevels[4] > min_qlevel[4]) {

            /* find the macroblock with the highest qlevel */
            for (i = 0; i < 5; i++) {
                if (qlevels[i] > min_qlevel[i] && qlevels[i] > qlevels[largest])
                    largest = i;
            }

            i = largest;

            /* ensure that we don't enter infinite loop */
            largest = (largest+1) % 5;

            if (qlevels[i] <= min_qlevel[i]) {
                /* can't unquantize any more */
                continue;
            }
            /* quantize a little bit less */
            save_qlevel = qlevels[i];
            qlevels[i] -= dv100_qlevel_inc;
            if (qlevels[i] < min_qlevel[i])
                qlevels[i] = min_qlevel[i];

            qnos[i] = DV100_QLEVEL_QNO(dv100_qlevels[qlevels[i]]);

            size[i] = 0;

            /* for each block */
            b = &blks[8*i];
            for (j = 0; j < 8; j++, b++) {
                /* accumulate block size into macroblock */
                if(size_cache[8*i+j][qlevels[i]] == 0) {
                    size_cache[8*i+j][qlevels[i]] = dv100_actual_quantize(b, qlevels[i]);
                }
                size[i] += size_cache[8*i+j][qlevels[i]];
            } /* for each block */

            /* did we bust the limit? */
            if (vs_total_ac_bits_hd < size[0] + size[1] + size[2] + size[3] + size[4]) {
                /* go back down and exit */
                qlevels[i] = save_qlevel;
                qnos[i] = DV100_QLEVEL_QNO(dv100_qlevels[qlevels[i]]);
                break;
            }
        }
    }

    /* now do the actual quantization */
    for (i = 0; i < 5; i++) {
        /* for each block */
        b = &blks[8*i];
        size[i] = 0;
        for (j = 0; j < 8; j++, b++) {
            /* accumulate block size into macroblock */
            size[i] += dv100_actual_quantize(b, qlevels[i]);
        } /* for each block */
    }
}

static inline void dv_guess_qnos(EncBlockInfo *blks, int *qnos)
{
    int size[5];
    int i, j, k, a, prev, a2;
    EncBlockInfo *b;

    size[0] =
    size[1] =
    size[2] =
    size[3] =
    size[4] = 1 << 24;
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
                        av_assert2(b->next[prev] >= mb_area_start[a + 1] || b->mb[prev]);
                        for (k = b->next[prev]; k < mb_area_start[a + 1]; k = b->next[k]) {
                            b->mb[k] >>= 1;
                            if (b->mb[k]) {
                                b->bit_size[a] += dv_rl2vlc_size(k - prev - 1, b->mb[k]);
                                prev            = k;
                            } else {
                                if (b->next[k] >= mb_area_start[a + 1] && b->next[k] < 64) {
                                    for (a2 = a + 1; b->next[k] >= mb_area_start[a2 + 1]; a2++)
                                        b->prev[a2] = prev;
                                    av_assert2(a2 < 4);
                                    av_assert2(b->mb[b->next[k]]);
                                    b->bit_size[a2] += dv_rl2vlc_size(b->next[k] - prev - 1, b->mb[b->next[k]]) -
                                                       dv_rl2vlc_size(b->next[k] - k    - 1, b->mb[b->next[k]]);
                                    av_assert2(b->prev[a2] == k && (a2 + 1 >= 4 || b->prev[a2 + 1] != k));
                                    b->prev[a2] = prev;
                                }
                                b->next[prev] = b->next[k];
                            }
                        }
                        b->prev[a + 1] = prev;
                    }
                    size[i] += b->bit_size[a];
                }
            }
            if (vs_total_ac_bits >= size[0] + size[1] + size[2] + size[3] + size[4])
                return;
        }
    } while (qnos[0] | qnos[1] | qnos[2] | qnos[3] | qnos[4]);

    for (a = 2; a == 2 || vs_total_ac_bits < size[0]; a += a) {
        b       = blks;
        size[0] = 5 * 6 * 4; // EOB
        for (j = 0; j < 6 * 5; j++, b++) {
            prev = b->prev[0];
            for (k = b->next[prev]; k < 64; k = b->next[k]) {
                if (b->mb[k] < a && b->mb[k] > -a) {
                    b->next[prev] = b->next[k];
                } else {
                    size[0] += dv_rl2vlc_size(k - prev - 1, b->mb[k]);
                    prev     = k;
                }
            }
        }
    }
}

/* update all cno values into the blocks, over-writing the old values without
   touching anything else. (only used for DV100) */
static inline void dv_revise_cnos(uint8_t *dif, EncBlockInfo *blk, const AVDVProfile *profile)
{
    uint8_t *data;
    int mb_index, i;

    for (mb_index = 0; mb_index < 5; mb_index++) {
        data = dif + mb_index*80 + 4;
        for (i = 0; i < profile->bpm; i++) {
            /* zero out the class number */
            data[1] &= 0xCF;
            /* add the new one */
            data[1] |= blk[profile->bpm*mb_index+i].cno << 4;

            data += profile->block_sizes[i] >> 3;
        }
    }
}

static int dv_encode_video_segment(AVCodecContext *avctx, void *arg)
{
    DVVideoContext *s = avctx->priv_data;
    DVwork_chunk *work_chunk = arg;
    int mb_index, i, j;
    int mb_x, mb_y, c_offset;
    ptrdiff_t linesize, y_stride;
    uint8_t *y_ptr;
    uint8_t *dif, *p;
    LOCAL_ALIGNED_8(uint8_t, scratch, [128]);
    EncBlockInfo enc_blks[5 * DV_MAX_BPM];
    PutBitContext pbs[5 * DV_MAX_BPM];
    PutBitContext *pb;
    EncBlockInfo *enc_blk;
    int vs_bit_size = 0;
    int qnos[5];
    int *qnosp = &qnos[0];

    p = dif = &s->buf[work_chunk->buf_offset * 80];
    enc_blk = &enc_blks[0];
    for (mb_index = 0; mb_index < 5; mb_index++) {
        dv_calculate_mb_xy(s, work_chunk, mb_index, &mb_x, &mb_y);

        qnos[mb_index] = DV_PROFILE_IS_HD(s->sys) ? 1 : 15;

        y_ptr    = s->frame->data[0] + ((mb_y * s->frame->linesize[0] + mb_x) << 3);
        linesize = s->frame->linesize[0];

        if (s->sys->height == 1080 && mb_y < 134)
            enc_blk->dct_mode = dv_guess_dct_mode(s, y_ptr, linesize);
        else
            enc_blk->dct_mode = 0;
        for (i = 1; i < 8; i++)
            enc_blk[i].dct_mode = enc_blk->dct_mode;

        /* initializing luminance blocks */
        if ((s->sys->pix_fmt == AV_PIX_FMT_YUV420P)                      ||
            (s->sys->pix_fmt == AV_PIX_FMT_YUV411P && mb_x >= (704 / 8)) ||
            (s->sys->height >= 720 && mb_y != 134)) {
            y_stride = s->frame->linesize[0] << (3*!enc_blk->dct_mode);
        } else {
            y_stride = 16;
        }
        y_ptr    = s->frame->data[0] +
                   ((mb_y * s->frame->linesize[0] + mb_x) << 3);
        linesize = s->frame->linesize[0];

        if (s->sys->video_stype == 4) { /* SD 422 */
            vs_bit_size +=
                dv_init_enc_block(enc_blk + 0, y_ptr,                linesize, s, 0) +
                dv_init_enc_block(enc_blk + 1, NULL,                 linesize, s, 0) +
                dv_init_enc_block(enc_blk + 2, y_ptr + 8,            linesize, s, 0) +
                dv_init_enc_block(enc_blk + 3, NULL,                 linesize, s, 0);
        } else {
            vs_bit_size +=
                dv_init_enc_block(enc_blk + 0, y_ptr,                linesize, s, 0) +
                dv_init_enc_block(enc_blk + 1, y_ptr + 8,            linesize, s, 0) +
                dv_init_enc_block(enc_blk + 2, y_ptr +     y_stride, linesize, s, 0) +
                dv_init_enc_block(enc_blk + 3, y_ptr + 8 + y_stride, linesize, s, 0);
        }
        enc_blk += 4;

        /* initializing chrominance blocks */
        c_offset = (((mb_y >>  (s->sys->pix_fmt == AV_PIX_FMT_YUV420P)) * s->frame->linesize[1] +
                     (mb_x >> ((s->sys->pix_fmt == AV_PIX_FMT_YUV411P) ? 2 : 1))) << 3);
        for (j = 2; j; j--) {
            uint8_t *c_ptr = s->frame->data[j] + c_offset;
            linesize = s->frame->linesize[j];
            y_stride = (mb_y == 134) ? 8 : (s->frame->linesize[j] << (3*!enc_blk->dct_mode));
            if (s->sys->pix_fmt == AV_PIX_FMT_YUV411P && mb_x >= (704 / 8)) {
                uint8_t *d;
                uint8_t *b = scratch;
                for (i = 0; i < 8; i++) {
                    d      = c_ptr + (linesize << 3);
                    b[0]   = c_ptr[0];
                    b[1]   = c_ptr[1];
                    b[2]   = c_ptr[2];
                    b[3]   = c_ptr[3];
                    b[4]   = d[0];
                    b[5]   = d[1];
                    b[6]   = d[2];
                    b[7]   = d[3];
                    c_ptr += linesize;
                    b     += 16;
                }
                c_ptr    = scratch;
                linesize = 16;
            }

            vs_bit_size += dv_init_enc_block(enc_blk++, c_ptr, linesize, s, 1);
            if (s->sys->bpm == 8)
                vs_bit_size += dv_init_enc_block(enc_blk++, c_ptr + y_stride,
                                                 linesize, s, 1);
        }
    }

    if (DV_PROFILE_IS_HD(s->sys)) {
        /* unconditional */
        dv_guess_qnos_hd(&enc_blks[0], qnosp);
    } else if (vs_total_ac_bits < vs_bit_size) {
        dv_guess_qnos(&enc_blks[0], qnosp);
    }

    /* DIF encoding process */
    for (j = 0; j < 5 * s->sys->bpm;) {
        int start_mb = j;

        p[3] = *qnosp++;
        p += 4;

        /* First pass over individual cells only */
        for (i = 0; i < s->sys->bpm; i++, j++) {
            int sz = s->sys->block_sizes[i] >> 3;

            init_put_bits(&pbs[j], p, sz);
            put_sbits(&pbs[j], 9, ((enc_blks[j].mb[0] >> 3) - 1024 + 2) >> 2);
            put_bits(&pbs[j], 1, DV_PROFILE_IS_HD(s->sys) && i ? 1 : enc_blks[j].dct_mode);
            put_bits(&pbs[j], 2, enc_blks[j].cno);

            dv_encode_ac(&enc_blks[j], &pbs[j], &pbs[j + 1]);
            p += sz;
        }

        /* Second pass over each MB space */
        pb = &pbs[start_mb];
        for (i = 0; i < s->sys->bpm; i++)
            if (enc_blks[start_mb + i].partial_bit_count)
                pb = dv_encode_ac(&enc_blks[start_mb + i], pb,
                                  &pbs[start_mb + s->sys->bpm]);
    }

    /* Third and final pass over the whole video segment space */
    pb = &pbs[0];
    for (j = 0; j < 5 * s->sys->bpm; j++) {
        if (enc_blks[j].partial_bit_count)
            pb = dv_encode_ac(&enc_blks[j], pb, &pbs[s->sys->bpm * 5]);
        if (enc_blks[j].partial_bit_count)
            av_log(avctx, AV_LOG_ERROR, "ac bitstream overflow\n");
    }

    for (j = 0; j < 5 * s->sys->bpm; j++) {
        int pos;
        int size = pbs[j].size_in_bits >> 3;
        flush_put_bits(&pbs[j]);
        pos = put_bits_count(&pbs[j]) >> 3;
        if (pos > size) {
            av_log(avctx, AV_LOG_ERROR,
                   "bitstream written beyond buffer size\n");
            return -1;
        }
        memset(pbs[j].buf + pos, 0xff, size - pos);
    }

    if (DV_PROFILE_IS_HD(s->sys))
        dv_revise_cnos(dif, enc_blks, s->sys);

    return 0;
}

static inline int dv_write_pack(enum dv_pack_type pack_id, DVVideoContext *c,
                                uint8_t *buf)
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
    uint8_t aspect = 0;
    int apt = (c->sys->pix_fmt == AV_PIX_FMT_YUV420P ? 0 : 1);
    int fs;

    if (c->avctx->height >= 720)
        fs = c->avctx->height == 720 || c->frame->top_field_first ? 0x40 : 0x00;
    else
        fs = c->frame->top_field_first ? 0x00 : 0x40;

    if (DV_PROFILE_IS_HD(c->sys) ||
        (int)(av_q2d(c->avctx->sample_aspect_ratio) *
              c->avctx->width / c->avctx->height * 10) >= 17)
        /* HD formats are always 16:9 */
        aspect = 0x02;

    buf[0] = (uint8_t) pack_id;
    switch (pack_id) {
    case dv_header525: /* I can't imagine why these two weren't defined as real */
    case dv_header625: /* packs in SMPTE314M -- they definitely look like ones */
        buf[1] =  0xf8       | /* reserved -- always 1 */
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
        buf[1] = 0xff;         /* reserved -- always 1 */
        buf[2] = (1 << 7) |    /* B/W: 0 - b/w, 1 - color */
                 (1 << 6) |    /* following CLF is valid - 0, invalid - 1 */
                 (3 << 4) |    /* CLF: color frames ID (see ITU-R BT.470-4) */
                 0xf;          /* reserved -- always 1 */
        buf[3] = (3 << 6)           | /* reserved -- always 1 */
                 (c->sys->dsf << 5) | /*  system: 60fields/50fields */
                 c->sys->video_stype; /* signal type video compression */
        buf[4] = 0xff;         /* VISC: 0xff -- no information */
        break;
    case dv_video_control:
        buf[1] = (0 << 6) |    /* Copy generation management (CGMS) 0 -- free */
                 0x3f;         /* reserved -- always 1 */
        buf[2] = 0xc8 |        /* reserved -- always b11001xxx */
                 aspect;
        buf[3] = (1 << 7) |    /* frame/field flag 1 -- frame, 0 -- field */
                 fs       |    /* first/second field flag 0 -- field 2, 1 -- field 1 */
                 (1 << 5) |    /* frame change flag 0 -- same picture as before, 1 -- different */
                 (1 << 4) |    /* 1 - interlaced, 0 - noninterlaced */
                 0xc;          /* reserved -- always b1100 */
        buf[4] = 0xff;         /* reserved -- always 1 */
        break;
    default:
        buf[1] =
        buf[2] =
        buf[3] =
        buf[4] = 0xff;
    }
    return 5;
}

static inline int dv_write_dif_id(enum dv_section_type t, uint8_t chan_num,
                                  uint8_t seq_num, uint8_t dif_num,
                                  uint8_t *buf)
{
    int fsc = chan_num & 1;
    int fsp = 1 - (chan_num >> 1);

    buf[0] = (uint8_t) t;      /* Section type */
    buf[1] = (seq_num  << 4) | /* DIF seq number 0-9 for 525/60; 0-11 for 625/50 */
             (fsc << 3) |      /* FSC: for 50 and 100Mb/s 0 - first channel; 1 - second */
             (fsp << 2) |      /* FSP: for 100Mb/s 1 - channels 0-1; 0 - channels 2-3 */
             3;                /* reserved -- always 1 */
    buf[2] = dif_num;          /* DIF block number Video: 0-134, Audio: 0-8 */
    return 3;
}

static inline int dv_write_ssyb_id(uint8_t syb_num, uint8_t fr, uint8_t *buf)
{
    if (syb_num == 0 || syb_num == 6) {
        buf[0] = (fr << 7) | /* FR ID 1 - first half of each channel; 0 - second */
                 (0  << 4) | /* AP3 (Subcode application ID) */
                 0x0f;       /* reserved -- always 1 */
    } else if (syb_num == 11) {
        buf[0] = (fr << 7) | /* FR ID 1 - first half of each channel; 0 - second */
                 0x7f;       /* reserved -- always 1 */
    } else {
        buf[0] = (fr << 7) | /* FR ID 1 - first half of each channel; 0 - second */
                 (0  << 4) | /* APT (Track application ID) */
                 0x0f;       /* reserved -- always 1 */
    }
    buf[1] = 0xf0 |            /* reserved -- always 1 */
             (syb_num & 0x0f); /* SSYB number 0 - 11   */
    buf[2] = 0xff;             /* reserved -- always 1 */
    return 3;
}

static void dv_format_frame(DVVideoContext *c, uint8_t *buf)
{
    int chan, i, j, k;
    /* We work with 720p frames split in half. The odd half-frame is chan 2,3 */
    int chan_offset = 2*(c->sys->height == 720 && c->avctx->frame_number & 1);

    for (chan = 0; chan < c->sys->n_difchan; chan++) {
        for (i = 0; i < c->sys->difseg_size; i++) {
            memset(buf, 0xff, 80 * 6); /* first 6 DIF blocks are for control data */

            /* DV header: 1DIF */
            buf += dv_write_dif_id(dv_sect_header, chan+chan_offset, i, 0, buf);
            buf += dv_write_pack((c->sys->dsf ? dv_header625 : dv_header525),
                                 c, buf);
            buf += 72; /* unused bytes */

            /* DV subcode: 2DIFs */
            for (j = 0; j < 2; j++) {
                buf += dv_write_dif_id(dv_sect_subcode, chan+chan_offset, i, j, buf);
                for (k = 0; k < 6; k++)
                    buf += dv_write_ssyb_id(k, (i < c->sys->difseg_size / 2), buf) + 5;
                buf += 29; /* unused bytes */
            }

            /* DV VAUX: 3DIFS */
            for (j = 0; j < 3; j++) {
                buf += dv_write_dif_id(dv_sect_vaux, chan+chan_offset, i, j, buf);
                buf += dv_write_pack(dv_video_source,  c, buf);
                buf += dv_write_pack(dv_video_control, c, buf);
                buf += 7 * 5;
                buf += dv_write_pack(dv_video_source,  c, buf);
                buf += dv_write_pack(dv_video_control, c, buf);
                buf += 4 * 5 + 2; /* unused bytes */
            }

            /* DV Audio/Video: 135 Video DIFs + 9 Audio DIFs */
            for (j = 0; j < 135; j++) {
                if (j % 15 == 0) {
                    memset(buf, 0xff, 80);
                    buf += dv_write_dif_id(dv_sect_audio, chan+chan_offset, i, j/15, buf);
                    buf += 77; /* audio control & shuffled PCM audio */
                }
                buf += dv_write_dif_id(dv_sect_video, chan+chan_offset, i, j, buf);
                buf += 77; /* 1 video macroblock: 1 bytes control
                            * 4 * 14 bytes Y 8x8 data
                            * 10 bytes Cr 8x8 data
                            * 10 bytes Cb 8x8 data */
            }
        }
    }
}

static int dvvideo_encode_frame(AVCodecContext *c, AVPacket *pkt,
                                const AVFrame *frame, int *got_packet)
{
    DVVideoContext *s = c->priv_data;
    int ret;

    if ((ret = ff_alloc_packet2(c, pkt, s->sys->frame_size, 0)) < 0)
        return ret;

    c->pix_fmt                = s->sys->pix_fmt;
    s->frame                  = frame;
#if FF_API_CODED_FRAME
FF_DISABLE_DEPRECATION_WARNINGS
    c->coded_frame->key_frame = 1;
    c->coded_frame->pict_type = AV_PICTURE_TYPE_I;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
    s->buf = pkt->data;

    dv_format_frame(s, pkt->data);

    c->execute(c, dv_encode_video_segment, s->work_chunks, NULL,
               dv_work_pool_size(s->sys), sizeof(DVwork_chunk));

    emms_c();

    pkt->flags |= AV_PKT_FLAG_KEY;
    *got_packet = 1;

    return 0;
}

#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
#define OFFSET(x) offsetof(DVVideoContext, x)
static const AVOption dv_options[] = {
    { "quant_deadzone",        "Quantizer dead zone",    OFFSET(quant_deadzone),       AV_OPT_TYPE_INT, { .i64 = 7 }, 0, 1024, VE },
    { NULL },
};

static const AVClass dvvideo_encode_class = {
    .class_name = "dvvideo encoder",
    .item_name  = av_default_item_name,
    .option     = dv_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_dvvideo_encoder = {
    .name           = "dvvideo",
    .long_name      = NULL_IF_CONFIG_SMALL("DV (Digital Video)"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_DVVIDEO,
    .priv_data_size = sizeof(DVVideoContext),
    .init           = dvvideo_encode_init,
    .encode2        = dvvideo_encode_frame,
    .capabilities   = AV_CODEC_CAP_SLICE_THREADS | AV_CODEC_CAP_FRAME_THREADS | AV_CODEC_CAP_INTRA_ONLY,
    .pix_fmts       = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_YUV411P, AV_PIX_FMT_YUV422P,
        AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE
    },
    .priv_class     = &dvvideo_encode_class,
};
