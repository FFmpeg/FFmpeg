/*
 * VC-9 and WMV3 decoder
 * Copyright (c) 2005 Anonymous
 * Copyright (c) 2005 Alex Beregszaszi
 * Copyright (c) 2005 Michael Niedermayer
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

/**
 * @file vc9.c
 * VC-9 and WMV3 decoder
 *
 * TODO: Norm-6 bitplane imode, most AP stuff, optimize, all of MB layer :)
 * TODO: use MPV_ !!
 */
#include "common.h"
#include "dsputil.h"
#include "avcodec.h"
#include "mpegvideo.h"
#include "vc9data.h"

#undef NDEBUG
#include <assert.h>

extern const uint32_t ff_table0_dc_lum[120][2], ff_table1_dc_lum[120][2];
extern const uint32_t ff_table0_dc_chroma[120][2], ff_table1_dc_chroma[120][2];
extern VLC ff_msmp4_dc_luma_vlc[2], ff_msmp4_dc_chroma_vlc[2];
#define MB_INTRA_VLC_BITS 9
extern VLC ff_msmp4_mb_i_vlc;
#define DC_VLC_BITS 9
static const uint16_t table_mb_intra[64][2];

/* Some inhibiting stuff */
#define HAS_ADVANCED_PROFILE   1
#define TRACE                  1

#if TRACE
#  define INIT_VLC(vlc, nb_bits, nb_codes, bits, bits_wrap, bits_size, \
                   codes, codes_wrap, codes_size, use_static)          \
  if (init_vlc(vlc, nb_bits, nb_codes, bits, bits_wrap, bits_size,     \
               codes, codes_wrap, codes_size, use_static) < 0)         \
  {                                                                    \
    av_log(v->s.avctx, AV_LOG_ERROR, "Error for " # vlc " (%i)\n", i);   \
    return -1;                                                         \
  }
#else
#  define INIT_VLC(vlc, nb_bits, nb_codes, bits, bits_wrap, bits_size, \
                   codes, codes_wrap, codes_size, use_static)          \
  init_vlc(vlc, nb_bits, nb_codes, bits, bits_wrap, bits_size,         \
           codes, codes_wrap, codes_size, use_static)
#endif

#define PROFILE_SIMPLE   0
#define PROFILE_MAIN     1
#define PROFILE_ADVANCED 3

#define QUANT_FRAME_IMPLICIT   0
#define QUANT_FRAME_EXPLICIT   1
#define QUANT_NON_UNIFORM      2
#define QUANT_UNIFORM          3

/* Where quant can be changed */
#define DQPROFILE_FOUR_EDGES   0
#define DQPROFILE_DOUBLE_EDGES 1
#define DQPROFILE_SINGLE_EDGE  2
#define DQPROFILE_ALL_MBS      3

/* Which edge is quantized with ALTPQUANT */
#define DQSINGLE_BEDGE_LEFT   0
#define DQSINGLE_BEDGE_TOP    1
#define DQSINGLE_BEDGE_RIGHT  2
#define DQSINGLE_BEDGE_BOTTOM 3

/* Which pair of edges is quantized with ALTPQUANT */
#define DQDOUBLE_BEDGE_TOPLEFT     0
#define DQDOUBLE_BEDGE_TOPRIGHT    1
#define DQDOUBLE_BEDGE_BOTTOMRIGHT 2
#define DQDOUBLE_BEDGE_BOTTOMLEFT  3

/* MV P modes */
#define MV_PMODE_1MV_HPEL_BILIN   0
#define MV_PMODE_1MV              1
#define MV_PMODE_1MV_HPEL         2
#define MV_PMODE_MIXED_MV         3
#define MV_PMODE_INTENSITY_COMP   4

#define BMV_TYPE_BACKWARD          0
#define BMV_TYPE_FORWARD           1
#define BMV_TYPE_INTERPOLATED      3

/* MV P mode - the 5th element is only used for mode 1 */
static const uint8_t mv_pmode_table[2][5] = {
  { MV_PMODE_1MV_HPEL_BILIN, MV_PMODE_1MV, MV_PMODE_1MV_HPEL, MV_PMODE_MIXED_MV, MV_PMODE_INTENSITY_COMP },
  { MV_PMODE_1MV, MV_PMODE_MIXED_MV, MV_PMODE_1MV_HPEL, MV_PMODE_1MV_HPEL_BILIN, MV_PMODE_INTENSITY_COMP }
};

/* One more frame type */
#define BI_TYPE 7

/* FIXME Worse than ugly */
static const int fps_nr[5] = { 24, 25, 30, 50, 60 },
  fps_dr[2] = { 1000, 1001 };
static const uint8_t pquant_table[3][32] = {
  {  /* Implicit quantizer */
     0,  1,  2,  3,  4,  5,  6,  7,  8,  6,  7,  8,  9, 10, 11, 12,
    13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 27, 29, 31
  },
  {  /* Explicit quantizer, pquantizer uniform */
     0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
  },
  {  /* Explicit quantizer, pquantizer non-uniform */
     0,  1,  1,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13,
    14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 29, 31
  }
};

// FIXME move this into the context
#define VC9_BFRACTION_VLC_BITS 7
static VLC vc9_bfraction_vlc;
#define VC9_IMODE_VLC_BITS 4
static VLC vc9_imode_vlc;
#define VC9_NORM2_VLC_BITS 3
static VLC vc9_norm2_vlc;
#if TILE_VLC_METHOD == 1
#define VC9_NORM6_VLC_BITS 9
static VLC vc9_norm6_vlc;
#endif
#if TILE_VLC_METHOD == 2
#define VC9_NORM6_FIRST_BITS 8
#define VC9_NORM6_SECOND_BITS 8
static VLC vc9_norm6_first, vc9_norm6_second;
#endif
/* Could be optimized, one table only needs 8 bits */
#define VC9_TTMB_VLC_BITS 9 //12
static VLC vc9_ttmb_vlc[3];
#define VC9_MV_DIFF_VLC_BITS 9 //15
static VLC vc9_mv_diff_vlc[4];
#define VC9_CBPCY_P_VLC_BITS 9 //14
static VLC vc9_cbpcy_p_vlc[4];
#define VC9_4MV_BLOCK_PATTERN_VLC_BITS 6
static VLC vc9_4mv_block_pattern_vlc[4];

//We mainly need data and is_raw, so this struct could be avoided
//to save a level of indirection; feel free to modify
typedef struct BitPlane {
    uint8_t *data;
    int width, stride;
    int height;
    uint8_t is_raw;
} BitPlane;

typedef struct VC9Context{
  /* No MpegEnc context, might be good to use it */
  MpegEncContext s;

  /***************************/
  /* Sequence Header         */
  /***************************/
  /* Simple/Main Profile */
  int res_sm; //reserved, 2b
  int res_x8; //reserved
  int multires; //frame-level RESPIC syntax element present
  int res_fasttx; //always 1
  int res_transtab; //always 0
  int rangered; //RANGEREDFRM (range reduction) syntax element present
  int res_rtm_flag; //reserved, set to 1
  int reserved; //duh

#if HAS_ADVANCED_PROFILE
  /* Advanced Profile */
  int level; //3
  int chromaformat; //2
  int postprocflag; //frame-based processing use
  int broadcast; //TFF/RFF present
    int interlace; //Progressive/interlaced (RPTFTM syntax element)
  int tfcntrflag; //TFCNTR present
  int panscanflag; //NUMPANSCANWIN, TOPLEFT{X,Y}, BOTRIGHT{X,Y} presents
    int extended_dmv;
  int color_prim; //8
  int transfer_char; //8
  int matrix_coef; //8
  int hrd_param_flag;
#endif

  /* All Profiles */
  /* TODO: move all int to flags */
  int profile; //2
  int frmrtq_postproc; //3
  int bitrtq_postproc; //5
    int loopfilter;
    int fastuvmc; //Rounding of qpel vector to hpel ? (not in Simple)
  int extended_mv; //Ext MV in P/B (not in Simple)
  int dquant; //How qscale varies with MBs, 2bits (not in Simple)
  int vstransform; //variable-size transform46
  

  int overlap; //overlapped transforms in use
  int quantizer_mode; //2, quantizer mode used for sequence, see QUANT_*
  int finterpflag; //INTERPFRM present


  /*****************************/
  /* Frame decoding            */
  /*****************************/
  /* All profiles */
  uint8_t mv_mode, mv_mode2; /* MV coding mode */
  uint8_t pq, altpq; /* Quantizers */
  uint8_t dquantfrm, dqprofile, dqsbedge, dqbilevel; /* pquant parameters */
  int tile; /* 3x2 if (width_mb%3) else 2x3 */
  int ac_table_level;
  VLC *luma_dc_vlc, *chroma_dc_vlc; /* transac/dcfrm bits are indexes */
  uint8_t ttmbf, ttfrm; /* Transform type */
  uint8_t lumscale, lumshift; /* Luma compensation parameters */
  int16_t bfraction; /* Relative position % anchors=> how to scale MVs */
  uint8_t halfpq; /* Uniform quant over image and qp+.5 */
  uint8_t respic;
  int buffer_fullness; /* For HRD ? */
  /* Ranges:
   * 0 -> [-64n 63.f] x [-32, 31.f]
   * 1 -> [-128, 127.f] x [-64, 63.f]
   * 2 -> [-512, 511.f] x [-128, 127.f]
   * 3 -> [-1024, 1023.f] x [-256, 255.f]
   */
  uint8_t mvrange;
  uint8_t pquantizer;
  uint8_t *previous_line_cbpcy; /* To use for predicted CBPCY */
  VLC *cbpcy_vlc /* Current CBPCY VLC table */,
    *ttmb_vlc /* Current MB Transform Type VLC table */;
  BitPlane mv_type_mb_plane; /* bitplane for mv_type == (4MV) */
  BitPlane skip_mb_plane, /* bitplane for skipped MBs */
    direct_mb_plane; /* bitplane for "direct" MBs */

  /* S/M only ? */
  uint8_t rangeredfrm; /* out_sample = CLIP((in_sample-128)*2+128) */
  uint8_t interpfrm;

#if HAS_ADVANCED_PROFILE
  /* Advanced */
  uint8_t fcm; //0->Progressive, 2->Frame-Interlace, 3->Field-Interlace
  uint8_t numpanscanwin;
  uint8_t tfcntr;
  uint8_t rptfrm, tff, rff;
  uint8_t topleftx;
  uint8_t toplefty;
  uint8_t bottomrightx;
  uint8_t bottomrighty;
  uint8_t uvsamp;
  uint8_t postproc;
  int hrd_num_leaky_buckets;
  uint8_t bit_rate_exponent;
  uint8_t buffer_size_exponent;
  BitPlane ac_pred_plane; //AC prediction flags bitplane
  BitPlane over_flags_plane; //Overflags bitplane
  uint8_t condover;
  uint16_t *hrd_rate, *hrd_buffer;
  VLC *luma_ac2_vlc, *chroma_ac2_vlc;
#endif
} VC9Context;

/* FIXME Slow and ugly */
static int get_prefix(GetBitContext *gb, int stop, int len)
{
#if 1
  int i = 0, tmp = !stop;

  while (i != len && tmp != stop)
  {
    tmp = get_bits(gb, 1);
    i++;
  }
  return i;
#else
  unsigned int buf;
  int log;

  OPEN_READER(re, gb);
  UPDATE_CACHE(re, gb);
  buf=GET_CACHE(re, gb); //Still not sure
  if (stop) buf = ~buf;
  
  log= av_log2(-buf); //FIXME: -?
  if (log < limit){
    LAST_SKIP_BITS(re, gb, log+1);
    CLOSE_READER(re, gb);
    return log;
  }
  
  LAST_SKIP_BITS(re, gb, limit);
  CLOSE_READER(re, gb);
  return limit;
#endif
}

static int vc9_init_common(VC9Context *v)
{
    static int done = 0;
    int i;

    /* Set the bit planes */
    /* FIXME memset better ? (16bytes) */
    v->mv_type_mb_plane = (struct BitPlane) { NULL, 0, 0, 0 };
    v->direct_mb_plane = (struct BitPlane) { NULL, 0, 0, 0 };
    v->skip_mb_plane = (struct BitPlane) { NULL, 0, 0, 0 };
#if HAS_ADVANCED_PROFILE
    v->ac_pred_plane = v->over_flags_plane = (struct BitPlane) { NULL, 0, 0, 0 };
    v->hrd_rate = v->hrd_buffer = NULL;
#endif

    /* VLC tables */
#if TILE_VLC_METHOD == 1
#  if 0 // spec -> actual tables converter
    for(i=0; i<64; i++){
        int code= (vc9_norm6_spec[i][1] << vc9_norm6_spec[i][4]) + vc9_norm6_spec[i][3];
        av_log(NULL, AV_LOG_DEBUG, "0x%03X, ", code);
        if(i%16==15) av_log(NULL, AV_LOG_DEBUG, "\n");
    }
    for(i=0; i<64; i++){
        int code= vc9_norm6_spec[i][2] + vc9_norm6_spec[i][4];
        av_log(NULL, AV_LOG_DEBUG, "%2d, ", code);
        if(i%16==15) av_log(NULL, AV_LOG_DEBUG, "\n");
    }
#  endif
#endif
    if(!done)
    {
        done = 1;
        INIT_VLC(&vc9_bfraction_vlc, VC9_BFRACTION_VLC_BITS, 23,
                 vc9_bfraction_bits, 1, 1,
                 vc9_bfraction_codes, 1, 1, 1);
        INIT_VLC(&vc9_norm2_vlc, VC9_NORM2_VLC_BITS, 4,
                 vc9_norm2_bits, 1, 1,
                 vc9_norm2_codes, 1, 1, 1);
#if TILE_VLC_METHOD == 1
        INIT_VLC(&vc9_norm6_vlc, VC9_NORM6_VLC_BITS, 64,
                 vc9_norm6_bits, 1, 1,
                 vc9_norm6_codes, 2, 2, 1);
#endif
#if TILE_VLC_METHOD == 2
        INIT_VLC(&vc9_norm6_first, VC9_NORM6_FIRST_BITS, 64,
                 &vc9_norm6_first[0][1], 1, 1,
                 &vc9_norm6_first[0][0], 1, 1, 1);
        INIT_VLC(&vc9_norm6_second, VC9_NORM6_SECOND_BITS, 64,
                 vc9_norm6_second[0][1], 1, 1,
                 vc9_norm6_second[0][1], 1, 1, 1);
#endif
        INIT_VLC(&vc9_imode_vlc, VC9_IMODE_VLC_BITS, 7,
                 vc9_imode_bits, 1, 1,
                 vc9_imode_codes, 1, 1, 1);
        for (i=0; i<3; i++)
        {
            INIT_VLC(&vc9_ttmb_vlc[i], VC9_TTMB_VLC_BITS, 16,
                     vc9_ttmb_bits[i], 1, 1,
                     vc9_ttmb_codes[i], 2, 2, 1);
        }
        for(i=0; i<4; i++)
        {
            INIT_VLC(&vc9_4mv_block_pattern_vlc[i], VC9_4MV_BLOCK_PATTERN_VLC_BITS, 16,
                     vc9_4mv_block_pattern_bits[i], 1, 1,
                     vc9_4mv_block_pattern_codes[i], 1, 1, 1);
            INIT_VLC(&vc9_cbpcy_p_vlc[i], VC9_CBPCY_P_VLC_BITS, 64,
                     vc9_cbpcy_p_bits[i], 1, 1,
                     vc9_cbpcy_p_codes[i], 2, 2, 1);
            INIT_VLC(&vc9_mv_diff_vlc[i], VC9_MV_DIFF_VLC_BITS, 73,
                     vc9_mv_diff_bits[i], 1, 1,
                     vc9_mv_diff_codes[i], 2, 2, 1);
        }
    }

    /* Other defaults */
    v->pq = -1;
    v->mvrange = 0; /* 7.1.1.18, p80 */

    return 0;
}

#if HAS_ADVANCED_PROFILE
/* 6.2.1, p32 */
static int decode_hrd(VC9Context *v, GetBitContext *gb)
{
    int i, num;

    num = get_bits(gb, 5);

    if (v->hrd_rate || num != v->hrd_num_leaky_buckets)
    {
        av_freep(&v->hrd_rate);
    }
    if (!v->hrd_rate) v->hrd_rate = av_malloc(num*sizeof(uint16_t));
    if (!v->hrd_rate) return -1;

    if (v->hrd_buffer || num != v->hrd_num_leaky_buckets)
    {
        av_freep(&v->hrd_buffer);
    }
    if (!v->hrd_buffer) v->hrd_buffer = av_malloc(num*sizeof(uint16_t));
    if (!v->hrd_buffer) return -1;

    v->hrd_num_leaky_buckets = num;

    //exponent in base-2 for rate
    v->bit_rate_exponent = get_bits(gb, 4);
    //exponent in base-2 for buffer_size
    v->buffer_size_exponent = get_bits(gb, 4);

    for (i=0; i<num; i++)
    {
        //mantissae, ordered (if not, use a function ?
        v->hrd_rate[i] = get_bits(gb, 16);
        if (i && v->hrd_rate[i-1]>=v->hrd_rate[i])
        {
            av_log(v->s.avctx, AV_LOG_ERROR, "HDR Rates aren't strictly increasing:"
                   "%i vs %i\n", v->hrd_rate[i-1], v->hrd_rate[i]);
            return -1;
        }
        v->hrd_buffer[i] = get_bits(gb, 16);
        if (i && v->hrd_buffer[i-1]<v->hrd_buffer[i])
        {
            av_log(v->s.avctx, AV_LOG_ERROR, "HDR Buffers aren't decreasing:"
                   "%i vs %i\n", v->hrd_buffer[i-1], v->hrd_buffer[i]);
            return -1;
        }
    }
    return 0;
}

/* Table 2, p18 */
static int decode_advanced_sequence_header(AVCodecContext *avctx, GetBitContext *gb)
{
    VC9Context *v = avctx->priv_data;
    int nr, dr, aspect_ratio;

    v->postprocflag = get_bits(gb, 1);
    v->broadcast = get_bits(gb, 1);
    v->interlace = get_bits(gb, 1);

    v->tfcntrflag = get_bits(gb, 1);
    v->finterpflag = get_bits(gb, 1); //common
    v->panscanflag = get_bits(gb, 1);
    v->reserved = get_bits(gb, 1);
    if (v->reserved)
    {
        av_log(avctx, AV_LOG_ERROR, "RESERVED should be 0 (is %i)\n",
               v->reserved);
        return -1;
    }
    if (v->extended_mv)
        v->extended_dmv = get_bits(gb, 1);

    /* 6.1.7, p21 */
    if (get_bits(gb, 1) /* pic_size_flag */)
    {
        avctx->coded_width = get_bits(gb, 12);
        avctx->coded_height = get_bits(gb, 12);
        if ( get_bits(gb, 1) /* disp_size_flag */)
        {
            avctx->width = get_bits(gb, 14);
            avctx->height = get_bits(gb, 14);
        }

        /* 6.1.7.4, p22 */
        if ( get_bits(gb, 1) /* aspect_ratio_flag */)
        {
            aspect_ratio = get_bits(gb, 4); //SAR
            if (aspect_ratio == 0x0F) //FF_ASPECT_EXTENDED
            {
                avctx->sample_aspect_ratio.num = get_bits(gb, 8);
                avctx->sample_aspect_ratio.den = get_bits(gb, 8);
            }
            else if (aspect_ratio == 0x0E)
            {
                av_log(avctx, AV_LOG_DEBUG, "Reserved AR found\n");
            }
            else
            {
              avctx->sample_aspect_ratio = vc9_pixel_aspect[aspect_ratio];
            }
        }
    }
    else
    {
        avctx->coded_width = avctx->width;
        avctx->coded_height = avctx->height;
    }

    /* 6.1.8, p23 */
    if ( !get_bits(gb, 1) /* framerateflag */)
    {
        if ( get_bits(gb, 1) /* framerateind */)
        {
            nr = get_bits(gb, 8);
            dr = get_bits(gb, 4);
            if (nr<1)
            {
                av_log(avctx, AV_LOG_ERROR, "0 is forbidden for FRAMERATENR\n");
                return -1;
            }
            if (nr>5)
            {
                av_log(avctx, AV_LOG_ERROR,
                       "Reserved FRAMERATENR %i not handled\n", nr);
           }
            if (dr<1)
            {
                av_log(avctx, AV_LOG_ERROR, "0 is forbidden for FRAMERATEDR\n");
           }
            if (dr>2)
            {
                av_log(avctx, AV_LOG_ERROR,
                       "Reserved FRAMERATEDR %i not handled\n", dr);
            }
            avctx->frame_rate_base = fps_nr[dr];
            avctx->frame_rate = fps_nr[nr];
        }
        else
        {
            nr = get_bits(gb, 16);
            // 0.03125->2048Hz / 0.03125Hz
            avctx->frame_rate = 1000000;
            avctx->frame_rate_base = 31250*(1+nr);
        }
    }

    /* 6.1.9, p25 */
    if ( get_bits(gb, 1) /* color_format_flag */)
    {
        //Chromacity coordinates of color primaries
        //like ITU-R BT.709-2, BT.470-2, ...
        v->color_prim = get_bits(gb, 8);
        if (v->color_prim<1)
        {
            av_log(avctx, AV_LOG_ERROR, "0 for COLOR_PRIM is reserved\n");
            return -1;
        }
        if (v->color_prim == 3 || v->color_prim>6)
        {
            av_log(avctx, AV_LOG_DEBUG, "Reserved COLOR_PRIM %i found\n",
                   v->color_prim);
            return -1;
        }

        //Opto-electronic transfer characteristics
        v->transfer_char = get_bits(gb, 8);
        if (v->transfer_char == 3 || v->transfer_char>8)
        {
            av_log(avctx, AV_LOG_DEBUG, "Reserved TRANSFERT_CHAR %i found\n",
                   v->color_prim);
            return -1;
        }

        //Matrix coefficient for primariev->YCbCr
        v->matrix_coef = get_bits(gb, 8);
        if (v->matrix_coef < 1) return -1; //forbidden
        if ((v->matrix_coef>3 && v->matrix_coef<6) || v->matrix_coef>7)
        {
            av_log(avctx, AV_LOG_DEBUG, "Reserved MATRIX_COEF %i found\n",
                   v->color_prim);
            return -1;
        }
    }

    //Hypothetical reference decoder indicator flag
    v->hrd_param_flag = get_bits(gb, 1);
    if (v->hrd_param_flag)
    {
      if (decode_hrd(v, gb) < 0) return -1;
    }

    av_log(avctx, AV_LOG_DEBUG, "Advanced profile not supported yet\n");
    return -1;
}
#endif

/* Figure 7-8, p16-17 */
static int decode_sequence_header(AVCodecContext *avctx, GetBitContext *gb)
{
    VC9Context *v = avctx->priv_data;

    v->profile = get_bits(gb, 2);
    av_log(avctx, AV_LOG_DEBUG, "Profile: %i\n", v->profile);

#if HAS_ADVANCED_PROFILE
    if (v->profile > PROFILE_MAIN)
    {
        v->level = get_bits(gb, 3);
        v->chromaformat = get_bits(gb, 2);
        if (v->chromaformat != 1)
        {
            av_log(avctx, AV_LOG_ERROR,
                   "Only 4:2:0 chroma format supported\n");
            return -1;
        }
    }
    else
#endif
    {
        v->res_sm = get_bits(gb, 2); //reserved
        if (v->res_sm)
        {
            av_log(avctx, AV_LOG_ERROR,
                   "Reserved RES_SM=%i is forbidden\n", v->res_sm);
            //return -1;
        }
    }

    // (fps-2)/4 (->30)
    v->frmrtq_postproc = get_bits(gb, 3); //common
    // (bitrate-32kbps)/64kbps
    v->bitrtq_postproc = get_bits(gb, 5); //common
    v->s.loop_filter = get_bits(gb, 1); //common

#if HAS_ADVANCED_PROFILE
    if (v->profile <= PROFILE_MAIN)
#endif
    {
        v->res_x8 = get_bits(gb, 1); //reserved
        if (v->res_x8)
        {
            av_log(avctx, AV_LOG_ERROR,
                   "1 for reserved RES_X8 is forbidden\n");
            //return -1;
        }
        v->multires = get_bits(gb, 1);
        v->res_fasttx = get_bits(gb, 1);
        if (!v->res_fasttx)
        {
            av_log(avctx, AV_LOG_ERROR,
                   "0 for reserved RES_FASTTX is forbidden\n");
            //return -1;
        }
    }

    v->fastuvmc =  get_bits(gb, 1); //common
    if (!v->profile && !v->fastuvmc)
    {
        av_log(avctx, AV_LOG_ERROR,
               "FASTUVMC unavailable in Simple Profile\n");
        return -1;
    }
    v->extended_mv =  get_bits(gb, 1); //common
    if (!v->profile && v->extended_mv)
    {
        av_log(avctx, AV_LOG_ERROR,
               "Extended MVs unavailable in Simple Profile\n");
        return -1;
    }
    v->dquant =  get_bits(gb, 2); //common
    v->vstransform =  get_bits(gb, 1); //common
    
#if HAS_ADVANCED_PROFILE
    if (v->profile <= PROFILE_MAIN)
#endif
    {
        v->res_transtab = get_bits(gb, 1);
        if (v->res_transtab)
        {
            av_log(avctx, AV_LOG_ERROR,
                   "1 for reserved RES_TRANSTAB is forbidden\n");
            return -1;
        }
    }

    v->overlap = get_bits(gb, 1); //common

#if HAS_ADVANCED_PROFILE
    if (v->profile <= PROFILE_MAIN)
#endif
    {
        v->s.resync_marker = get_bits(gb, 1);
        v->rangered = get_bits(gb, 1);
    }

    v->s.max_b_frames = avctx->max_b_frames = get_bits(gb, 3); //common
    v->quantizer_mode = get_bits(gb, 2); //common

#if HAS_ADVANCED_PROFILE
    if (v->profile <= PROFILE_MAIN)
#endif
    {
        v->finterpflag = get_bits(gb, 1); //common
        v->res_rtm_flag = get_bits(gb, 1); //reserved
        if (!v->res_rtm_flag)
        {
            av_log(avctx, AV_LOG_ERROR,
                   "0 for reserved RES_RTM_FLAG is forbidden\n");
            //return -1;
        }
#if TRACE
        av_log(avctx, AV_LOG_INFO,
               "Profile %i:\nfrmrtq_postproc=%i, bitrtq_postproc=%i\n"
               "LoopFilter=%i, MultiRes=%i, FastUVMV=%i, Extended MV=%i\n"
               "Rangered=%i, VSTransform=%i, Overlap=%i, SyncMarker=%i\n"
               "DQuant=%i, Quantizer mode=%i, Max B frames=%i\n",
               v->profile, v->frmrtq_postproc, v->bitrtq_postproc,
               v->loopfilter, v->multires, v->fastuvmc, v->extended_mv,
               v->rangered, v->vstransform, v->overlap, v->s.resync_marker,
               v->dquant, v->quantizer_mode, avctx->max_b_frames
               );
        return 0;
#endif
    }
#if HAS_ADVANCED_PROFILE
    else return decode_advanced_sequence_header(avctx, gb);
#endif
}


#if HAS_ADVANCED_PROFILE
/*****************************************************************************/
/* Entry point decoding (Advanced Profile)                                   */
/*****************************************************************************/
static int advanced_entry_point_process(AVCodecContext *avctx, GetBitContext *gb)
{
    VC9Context *v = avctx->priv_data;
    int range_mapy_flag, range_mapuv_flag, i;
    if (v->profile != PROFILE_ADVANCED)
    {
        av_log(avctx, AV_LOG_ERROR,
               "Entry point are only defined in Advanced Profile!\n");
        return -1; //Only for advanced profile!
    }
    if (v->hrd_param_flag)
    {
        //Update buffer fullness
        av_log(avctx, AV_LOG_DEBUG, "Buffer fullness update\n");
        for (i=0; i<v->hrd_num_leaky_buckets; i++)
            skip_bits(gb, 8);
    }
    if ((range_mapy_flag = get_bits(gb, 1)))
    {
        //RANGE_MAPY
        av_log(avctx, AV_LOG_DEBUG, "RANGE_MAPY\n");
        skip_bits(gb, 3);
    }
    if ((range_mapuv_flag = get_bits(gb, 1)))
    {
        //RANGE_MAPUV
        av_log(avctx, AV_LOG_DEBUG, "RANGE_MAPUV\n");
        skip_bits(gb, 3);
    }
    if (v->panscanflag)
    {
        //NUMPANSCANWIN
        v->numpanscanwin = get_bits(gb, 3);
        av_log(avctx, AV_LOG_DEBUG, "NUMPANSCANWIN: %u\n", v->numpanscanwin);
    }
    return 0;
}
#endif

/******************************************************************************/
/* Bitplane decoding: 8.7, p56                                                */
/******************************************************************************/
#define IMODE_RAW     0
#define IMODE_NORM2   1
#define IMODE_DIFF2   2
#define IMODE_NORM6   3
#define IMODE_DIFF6   4
#define IMODE_ROWSKIP 5
#define IMODE_COLSKIP 6
int alloc_bitplane(BitPlane *bp, int width, int height)
{
    if (!bp || bp->width<0 || bp->height<0) return -1;
    bp->data = (uint8_t*)av_malloc(width*height);
    if (!bp->data) return -1;
    bp->width = bp->stride = width; //FIXME Needed for aligned data ?
    bp->height = height;
    return 0;
}

void free_bitplane(BitPlane *bp)
{
    bp->width = bp->stride = bp->height = 0;
    if (bp->data) av_freep(&bp->data);
}

static void decode_rowskip(uint8_t* plane, int width, int height, int stride, VC9Context *v){
    int x, y;
    GetBitContext *gb = &v->s.gb;

    for (y=0; y<height; y++){
        if (!get_bits(gb, 1)) //rowskip
            memset(plane, 0, width);
        else
            for (x=0; x<width; x++) 
                plane[x] = get_bits(gb, 1);
        plane += stride;
    }
}

//FIXME optimize
static void decode_colskip(uint8_t* plane, int width, int height, int stride, VC9Context *v){
    int x, y;
    GetBitContext *gb = &v->s.gb;

    for (x=0; x<width; x++){
        if (!get_bits(gb, 1)) //colskip
            for (y=0; y<height; y++)
                plane[y*stride] = 0;
        else
            for (y=0; y<height; y++)
                plane[y*stride] = get_bits(gb, 1);
        plane ++;
    }
}

//FIXME optimize
//FIXME Use BitPlane struct or return if table is raw (no bits read here but
//      later on)
//Elements must be either 0 or 1
static int bitplane_decoding(BitPlane *bp, VC9Context *v)
{
    GetBitContext *gb = &v->s.gb;

    int imode, x, y, code, use_vertical_tile, tile_w, tile_h;
    uint8_t invert, *planep = bp->data;

    invert = get_bits(gb, 1);
    imode = get_vlc2(gb, vc9_imode_vlc.table, VC9_IMODE_VLC_BITS, 2);

    bp->is_raw = 0;
    switch (imode)
    {
    case IMODE_RAW:
        //Data is actually read in the MB layer (same for all tests == "raw")
        bp->is_raw = 1; //invert ignored
        return invert;
    case IMODE_DIFF2:
    case IMODE_NORM2:
        if ((bp->height*bp->width) & 1) *(++planep) = get_bits(gb, 1);
        for(x=0; x<(bp->height*bp->width)>>1; x++){
            code = get_vlc2(gb, vc9_norm2_vlc.table, VC9_NORM2_VLC_BITS, 2);
            *(++planep) = code&1; //lsb => left
            *(++planep) = (code>>1)&1; //msb => right
            //FIXME width->stride
        }
        break;
    case IMODE_DIFF6:
    case IMODE_NORM6:
        use_vertical_tile=  bp->height%3==0 &&  bp->width%3!=0;
        tile_w= use_vertical_tile ? 2 : 3;
        tile_h= use_vertical_tile ? 3 : 2;

        for(y=  bp->height%tile_h; y< bp->height; y+=tile_h){
            for(x=  bp->width%tile_w; x< bp->width; x+=tile_w){
#if TILE_VLC_METHOD == 1 //FIXME Too much optimized ?
                code = get_vlc2(gb, vc9_norm6_vlc.table, VC9_NORM6_VLC_BITS, 2);
                if(code<0){
                    av_log(v->s.avctx, AV_LOG_DEBUG, "inavlid NORM-6 VLC\n");
                    return -1;
                }
#endif
#if TILE_VLC_METHOD == 2 //TODO Optimize VLC decoding
                code = get_vlc2(gb, vc9_norm6_first.table, VC9_NORM6_FIRST_BITS, 2);
                if (vc9_norm6_mode[code] == 1)
                {
#  if TRACE
                    code = get_bits(gb, 5);
                    assert(code>-1 && code<20);
                    code = vc9_norm6_flc_val[code];
#  else
                    code = vc9_norm6_flc_val[get_bits(gb, 5)];
#  endif
                }
                else if (vc9_norm6_mode[code] == 2)
                {
#  if TRACE
                    code = get_vlc2(gb, vc9_norm6_second.table, VC9_NORM6_SECOND_BITS, 2);
                    assert(code>-1 && code<22);
                    code = vc9_norm6_second_val[code];
#  else
                    code = vc9_norm6_second_val[get_vlc2(gb, vc9_norm6_second.table, VC9_NORM6_SECOND_BITS, 2)];
#  endif
#endif //TILE_VLC_METHOD == 2
                //FIXME following is a pure guess and probably wrong
                //FIXME A bitplane (0 | !0), so could the shifts be avoided ?
                planep[x     + 0*bp->stride]= (code>>0)&1;
                planep[x + 1 + 0*bp->stride]= (code>>1)&1;
                //FIXME Does branch prediction help here?
                if(use_vertical_tile){
                    planep[x + 0 + 1*bp->stride]= (code>>2)&1;
                    planep[x + 1 + 1*bp->stride]= (code>>3)&1;
                    planep[x + 0 + 2*bp->stride]= (code>>4)&1;
                    planep[x + 1 + 2*bp->stride]= (code>>5)&1;
                }else{
                    planep[x + 2 + 0*bp->stride]= (code>>2)&1;
                    planep[x + 0 + 1*bp->stride]= (code>>3)&1;
                    planep[x + 1 + 1*bp->stride]= (code>>4)&1;
                    planep[x + 2 + 1*bp->stride]= (code>>5)&1;
                }
            }
        }

        x=  bp->width % tile_w;
        decode_colskip(bp->data  ,             x, bp->height         , bp->stride, v);
        decode_rowskip(bp->data+x, bp->width - x, bp->height % tile_h, bp->stride, v);

        break;
    case IMODE_ROWSKIP:
        decode_rowskip(bp->data, bp->width, bp->height, bp->stride, v);
        break;
    case IMODE_COLSKIP: //Teh ugly
        decode_colskip(bp->data, bp->width, bp->height, bp->stride, v);
        break;
    default: break;
    }

    /* Applying diff operator */
    if (imode == IMODE_DIFF2 || imode == IMODE_DIFF6)
    {
        planep = bp->data;
        planep[0] ^= invert;
        for (x=1; x<bp->width; x++)
            planep[x] ^= planep[x-1];
        for (y=1; y<bp->height; y++)
        {
            planep += bp->stride;
            planep[0] ^= planep[-bp->stride];
            for (x=1; x<bp->width; x++)
            {
                if (planep[x-1] != planep[x-bp->stride]) planep[x] ^= invert;
                else                                     planep[x] ^= planep[x-1];
            }
        }
    }
    else if (invert)
    {
        planep = bp->data;
        for (x=0; x<bp->width*bp->height; x++) planep[x] = !planep[x]; //FIXME stride
    }
    return (imode<<1) + invert;
}

/*****************************************************************************/
/* VOP Dquant decoding                                                       */
/*****************************************************************************/
static int vop_dquant_decoding(VC9Context *v)
{
    GetBitContext *gb = &v->s.gb;
    int pqdiff;

    //variable size
    if (v->dquant == 2)
    {
        pqdiff = get_bits(gb, 3);
        if (pqdiff == 7) v->altpq = get_bits(gb, 5);
        else v->altpq = v->pq + pqdiff + 1;
    }
    else
    {
        v->dquantfrm = get_bits(gb, 1);
        if ( v->dquantfrm )
        {
            v->dqprofile = get_bits(gb, 2);
            switch (v->dqprofile)
            {
            case DQPROFILE_SINGLE_EDGE:
            case DQPROFILE_DOUBLE_EDGES:
                v->dqsbedge = get_bits(gb, 2);
                break;
            case DQPROFILE_ALL_MBS:
                v->dqbilevel = get_bits(gb, 1);
            default: break; //Forbidden ?
            }
            if (!v->dqbilevel || v->dqprofile != DQPROFILE_ALL_MBS)
            {
                pqdiff = get_bits(gb, 3);
                if (pqdiff == 7) v->altpq = get_bits(gb, 5);
                else v->altpq = v->pq + pqdiff + 1;
            }
        }
    }
    return 0;
}

/*****************************************************************************/
/* All Profiles picture header decoding specific functions                   */
/* Only pro/epilog differs between Simple/Main and Advanced => check caller  */
/*****************************************************************************/

/* Tables 11+12, p62-65 */
static int decode_b_picture_primary_header(VC9Context *v)
{
    GetBitContext *gb = &v->s.gb;
    int pqindex, status;

    /* Prolog common to all frametypes should be done in caller */
    if (v->profile == PROFILE_SIMPLE)
    {
        av_log(v->s.avctx, AV_LOG_ERROR, "Found a B frame while in Simple Profile!\n");
        return FRAME_SKIPED;
    }

    v->bfraction = vc9_bfraction_lut[get_vlc2(gb, vc9_bfraction_vlc.table,
                                              VC9_BFRACTION_VLC_BITS, 2)];
    if (v->bfraction < -1)
    {
        av_log(v->s.avctx, AV_LOG_ERROR, "Invalid BFRaction\n");
        return FRAME_SKIPED;
    }
    else if (!v->bfraction)
    {
        /* We actually have a BI frame */
        v->s.pict_type = BI_TYPE;
        v->buffer_fullness = get_bits(gb, 7);
    }

    /* Read the quantization stuff */
    pqindex = get_bits(gb, 5);
    if (v->quantizer_mode == QUANT_FRAME_IMPLICIT)
        v->pq = pquant_table[0][pqindex];
    else
    {
        v->pq = pquant_table[v->quantizer_mode-1][pqindex];
    }
    if (pqindex < 9) v->halfpq = get_bits(gb, 1);
    if (v->quantizer_mode == QUANT_FRAME_EXPLICIT)
        v->pquantizer = get_bits(gb, 1);

    /* Read the MV type/mode */
    if (v->extended_mv == 1)
        v->mvrange = get_prefix(gb, 0, 3);
    if (v->s.pict_type != BI_TYPE)
    {
        v->mv_mode = get_bits(gb, 1);
        if (v->pq < 13)
        {
            if (!v->mv_mode)
            {
                v->mv_mode = get_bits(gb, 2);
                if (v->mv_mode)
                av_log(v->s.avctx, AV_LOG_ERROR,
                       "mv_mode for lowquant B frame was %i\n", v->mv_mode);
            }
        }
        else
        {
            if (!v->mv_mode)
            {
                if (get_bits(gb, 1))
                     av_log(v->s.avctx, AV_LOG_ERROR,
                            "mv_mode for highquant B frame was %i\n", v->mv_mode);
            }
            v->mv_mode = 1-v->mv_mode; //To match (pq < 13) mapping
        }
    }

    return 0;
}

static int decode_b_picture_secondary_header(VC9Context *v)
{
    GetBitContext *gb = &v->s.gb;
    int status;

    bitplane_decoding(&v->skip_mb_plane, v);
    if (status < 0) return -1;
#if TRACE
    if (v->mv_mode == MV_PMODE_MIXED_MV)
    {
        status = bitplane_decoding(&v->mv_type_mb_plane, v);
        if (status < 0)
            return -1;
#if TRACE
        av_log(v->s.avctx, AV_LOG_DEBUG, "MB MV Type plane encoding: "
               "Imode: %i, Invert: %i\n", status>>1, status&1);
#endif
    }

    //bitplane
    status = bitplane_decoding(&v->direct_mb_plane, v);
    if (status < 0) return -1;
#if TRACE
    av_log(v->s.avctx, AV_LOG_DEBUG, "MB Direct plane encoding: "
           "Imode: %i, Invert: %i\n", status>>1, status&1);
#endif

    av_log(v->s.avctx, AV_LOG_DEBUG, "Skip MB plane encoding: "
           "Imode: %i, Invert: %i\n", status>>1, status&1);
#endif

    /* FIXME: what is actually chosen for B frames ? */
    v->s.mv_table_index = get_bits(gb, 2); //but using vc9_ tables
    v->cbpcy_vlc = &vc9_cbpcy_p_vlc[get_bits(gb, 2)];

    if (v->dquant)
    {
        vop_dquant_decoding(v);
    }

    if (v->vstransform)
    {
        v->ttmbf = get_bits(gb, 1);
        if (v->ttmbf)
        {
            v->ttfrm = get_bits(gb, 2);
            av_log(v->s.avctx, AV_LOG_INFO, "Transform used: %ix%i\n",
                   (v->ttfrm & 2) ? 4 : 8, (v->ttfrm & 1) ? 4 : 8);
        }
    }
    /* Epilog (AC/DC syntax) should be done in caller */
    return 0;
}

/* Tables 5+7, p53-54 and 55-57 */
static int decode_i_picture_header(VC9Context *v)
{
    GetBitContext *gb = &v->s.gb;
    int pqindex, status = 0;

    /* Prolog common to all frametypes should be done in caller */
    //BF = Buffer Fullness
    if (v->profile <= PROFILE_MAIN && get_bits(gb, 7))
    {
        av_log(v->s.avctx, AV_LOG_DEBUG, "I BufferFullness not 0\n");
    }

    /* Quantizer stuff */
    pqindex = get_bits(gb, 5);
    if (v->quantizer_mode == QUANT_FRAME_IMPLICIT)
        v->pq = pquant_table[0][pqindex];
    else
    {
        v->pq = pquant_table[v->quantizer_mode-1][pqindex];
    }
    if (pqindex < 9) v->halfpq = get_bits(gb, 1);
    if (v->quantizer_mode == QUANT_FRAME_EXPLICIT)
        v->pquantizer = get_bits(gb, 1);
    av_log(v->s.avctx, AV_LOG_DEBUG, "I frame: QP=%i (+%i/2)\n",
           v->pq, v->halfpq);
#if HAS_ADVANCED_PROFILE
    if (v->profile <= PROFILE_MAIN)
#endif
    {
        if (v->extended_mv) v->mvrange = get_prefix(gb, 0, 3);
        if (v->multires) v->respic = get_bits(gb, 2);
    }
#if HAS_ADVANCED_PROFILE
    else
    {
        v->s.ac_pred = get_bits(gb, 1);
        if (v->postprocflag) v->postproc = get_bits(gb, 1);
        /* 7.1.1.34 + 8.5.2 */
        if (v->overlap && v->pq<9)
        {
            v->condover = get_bits(gb, 1);
            if (v->condover)
            {
                v->condover = 2+get_bits(gb, 1);
                if (v->condover == 3)
                {
                    status = bitplane_decoding(&v->over_flags_plane, v);
                    if (status < 0) return -1;
#if TRACE
                    av_log(v->s.avctx, AV_LOG_DEBUG, "Overflags plane encoding: "
                           "Imode: %i, Invert: %i\n", status>>1, status&1);
#endif
                }
            }
        }
    }
#endif

    /* Epilog (AC/DC syntax) should be done in caller */
    return status;
}

/* Table 9, p58-60 */
static int decode_p_picture_primary_header(VC9Context *v)
{
    /* INTERFRM, FRMCNT, RANGEREDFRM read in caller */
    GetBitContext *gb = &v->s.gb;
    int lowquant, pqindex, status = 0;

    pqindex = get_bits(gb, 5);
    if (v->quantizer_mode == QUANT_FRAME_IMPLICIT)
        v->pq = pquant_table[0][pqindex];
    else
    {
        v->pq = pquant_table[v->quantizer_mode-1][pqindex];
    }
    if (pqindex < 9) v->halfpq = get_bits(gb, 1);
    if (v->quantizer_mode == QUANT_FRAME_EXPLICIT)
        v->pquantizer = get_bits(gb, 1);
    av_log(v->s.avctx, AV_LOG_DEBUG, "P Frame: QP=%i (+%i/2)\n",
           v->pq, v->halfpq);
    if (v->extended_mv == 1) v->mvrange = get_prefix(gb, 0, 3);
#if HAS_ADVANCED_PROFILE
    if (v->profile > PROFILE_MAIN)
    {
        if (v->postprocflag) v->postproc = get_bits(gb, 1);
    }
    else
#endif
        if (v->multires) v->respic = get_bits(gb, 2);
    lowquant = (v->pquantizer>12) ? 0 : 1;
    v->mv_mode = mv_pmode_table[lowquant][get_prefix(gb, 1, 4)];
    if (v->mv_mode == MV_PMODE_INTENSITY_COMP)
    {
        v->mv_mode2 = mv_pmode_table[lowquant][get_prefix(gb, 1, 3)];
        v->lumscale = get_bits(gb, 6);
        v->lumshift = get_bits(gb, 6);
    }
    return 0;
}

static int decode_p_picture_secondary_header(VC9Context *v)
{
    GetBitContext *gb = &v->s.gb;
    int status = 0;
    if ((v->mv_mode == MV_PMODE_INTENSITY_COMP &&
         v->mv_mode2 == MV_PMODE_MIXED_MV)
        || v->mv_mode == MV_PMODE_MIXED_MV)
    {
        status = bitplane_decoding(&v->mv_type_mb_plane, v);
        if (status < 0) return -1;
#if TRACE
        av_log(v->s.avctx, AV_LOG_DEBUG, "MB MV Type plane encoding: "
               "Imode: %i, Invert: %i\n", status>>1, status&1);
#endif
    }

    status = bitplane_decoding(&v->skip_mb_plane, v);
    if (status < 0) return -1;
#if TRACE
    av_log(v->s.avctx, AV_LOG_DEBUG, "MB Skip plane encoding: "
           "Imode: %i, Invert: %i\n", status>>1, status&1);
#endif

    /* Hopefully this is correct for P frames */
    v->s.mv_table_index =get_bits(gb, 2); //but using vc9_ tables
    v->cbpcy_vlc = &vc9_cbpcy_p_vlc[get_bits(gb, 2)];

    if (v->dquant)
    {
        av_log(v->s.avctx, AV_LOG_INFO, "VOP DQuant info\n");
        vop_dquant_decoding(v);
    }

    v->ttfrm = 0; //FIXME Is that so ?
    if (v->vstransform)
    {
        v->ttmbf = get_bits(gb, 1);
        if (v->ttmbf)
        {
            v->ttfrm = get_bits(gb, 2);
            av_log(v->s.avctx, AV_LOG_INFO, "Transform used: %ix%i\n",
                   (v->ttfrm & 2) ? 4 : 8, (v->ttfrm & 1) ? 4 : 8);
        }
    }
    /* Epilog (AC/DC syntax) should be done in caller */
    return 0;
}


static int standard_decode_picture_primary_header(VC9Context *v)
{
    GetBitContext *gb = &v->s.gb;
    int status = 0;

    if (v->finterpflag) v->interpfrm = get_bits(gb, 1);
    skip_bits(gb, 2); //framecnt unused
    if (v->rangered) v->rangeredfrm = get_bits(gb, 1);
    v->s.pict_type = get_bits(gb, 1);
    if (v->s.avctx->max_b_frames)
    {
        if (!v->s.pict_type)
        {
            if (get_bits(gb, 1)) v->s.pict_type = I_TYPE;
            else v->s.pict_type = B_TYPE;
        }
        else v->s.pict_type = P_TYPE;
    }
    else v->s.pict_type++;

    switch (v->s.pict_type)
    {
    case I_TYPE: status = decode_i_picture_header(v); break;
    case P_TYPE: status = decode_p_picture_primary_header(v); break;
    case BI_TYPE:
    case B_TYPE: status = decode_b_picture_primary_header(v); break;
    }

    if (status == FRAME_SKIPED)
    {
      av_log(v->s.avctx, AV_LOG_INFO, "Skipping frame...\n");
      return status;
    }
    return 0;
}

static int standard_decode_picture_secondary_header(VC9Context *v)
{
    GetBitContext *gb = &v->s.gb;
    int status = 0, index;

    switch (v->s.pict_type)
    {
    case P_TYPE: status = decode_p_picture_secondary_header(v); break;
    case B_TYPE: status = decode_b_picture_secondary_header(v); break;
    }

    /* AC Syntax */
    v->ac_table_level = decode012(gb);
    if (v->s.pict_type == I_TYPE || v->s.pict_type == BI_TYPE)
    {
        index = decode012(gb);
        v->luma_ac2_vlc = NULL + index; //FIXME Add AC2 table
        v->chroma_ac2_vlc = NULL + index;
    }
    /* DC Syntax */
    index = decode012(gb);
    v->luma_dc_vlc = &ff_msmp4_dc_luma_vlc[index];
    v->chroma_dc_vlc = &ff_msmp4_dc_chroma_vlc[index];
   
    return 0;
}


#if HAS_ADVANCED_PROFILE
/******************************************************************************/
/* Advanced Profile picture header decoding specific functions                */
/******************************************************************************/
static int advanced_decode_picture_primary_header(VC9Context *v)
{
    GetBitContext *gb = &v->s.gb;
    static const int type_table[4] = { P_TYPE, B_TYPE, I_TYPE, BI_TYPE };
    int type, i;

    if (v->interlace)
    {
        v->fcm = get_bits(gb, 1);
        if (v->fcm) v->fcm = 2+get_bits(gb, 1);
    }

    type = get_prefix(gb, 0, 4);
    if (type > 4 || type < 0) return FRAME_SKIPED;
    v->s.pict_type = type_table[type];
    av_log(v->s.avctx, AV_LOG_INFO, "AP Frame Type: %i\n", v->s.pict_type);

    if (v->tfcntrflag) v->tfcntr = get_bits(gb, 8);
    if (v->broadcast)
    {
        if (!v->interlace) v->rptfrm = get_bits(gb, 2);
        else
        {
            v->tff = get_bits(gb, 1);
            v->rff = get_bits(gb, 1);
        }
    }

    if (v->panscanflag)
    {
#if 0
        for (i=0; i<v->numpanscanwin; i++)
        {
            v->topleftx[i] = get_bits(gb, 16);
            v->toplefty[i] = get_bits(gb, 16);
            v->bottomrightx[i] = get_bits(gb, 16);
            v->bottomrighty[i] = get_bits(gb, 16);
        }
#else
        skip_bits(gb, 16*4*v->numpanscanwin);
#endif
    }
    v->s.no_rounding = !get_bits(gb, 1);
    v->uvsamp = get_bits(gb, 1);
    if (v->finterpflag == 1) v->interpfrm = get_bits(gb, 1);

    switch(v->s.pict_type)
    {
    case I_TYPE: if (decode_i_picture_header(v) < 0) return -1;
    case P_TYPE: if (decode_p_picture_primary_header(v) < 0) return -1;
    case BI_TYPE:
    case B_TYPE: if (decode_b_picture_primary_header(v) < 0) return FRAME_SKIPED;
    default: break;
    }
    return 0;
}

static int advanced_decode_picture_secondary_header(VC9Context *v)
{
    GetBitContext *gb = &v->s.gb;
    int index;

    switch(v->s.pict_type)
    {
    case P_TYPE: if (decode_p_picture_secondary_header(v) < 0) return -1;
    case B_TYPE: if (decode_b_picture_secondary_header(v) < 0) return FRAME_SKIPED;
    default: break;
    }

    /* AC Syntax */
    v->ac_table_level = decode012(gb);
    if (v->s.pict_type == I_TYPE || v->s.pict_type == BI_TYPE)
    {
        index = decode012(gb); //FIXME
        v->luma_ac2_vlc = NULL + index;
        v->chroma_ac2_vlc = NULL + index;
    }
    /* DC Syntax */
    index = decode012(gb);
    v->luma_dc_vlc = &ff_msmp4_dc_luma_vlc[index];
    v->chroma_dc_vlc = &ff_msmp4_dc_chroma_vlc[index];

    return 0;
}
#endif

/******************************************************************************/
/* Block decoding functions                                                   */
/******************************************************************************/
/* 7.1.4, p91 and 8.1.1.7, p(1)04 */
/* FIXME proper integration (unusable and lots of parameters to send */
int decode_luma_intra_block(VC9Context *v, int mquant)
{
    GetBitContext *gb = &v->s.gb;
    int dcdiff;

    dcdiff = get_vlc2(gb, v->luma_dc_vlc->table,
                      DC_VLC_BITS, 2);
    if (dcdiff)
    {
        if (dcdiff == 119 /* ESC index value */)
        {
            /* TODO: Optimize */
            if (mquant == 1) dcdiff = get_bits(gb, 10);
            else if (mquant == 2) dcdiff = get_bits(gb, 9);
            else dcdiff = get_bits(gb, 8);
        }
        else
        {
            if (mquant == 1)
                dcdiff = (dcdiff<<2) + get_bits(gb, 2) - 3;
            else if (mquant == 2)
                dcdiff = (dcdiff<<1) + get_bits(gb, 1) - 1;
        }
        if (get_bits(gb, 1))
            dcdiff = -dcdiff;
    }
    /* FIXME: 8.1.1.15, p(1)13, coeff scaling for Adv Profile */

    return 0;
}

/******************************************************************************/
/* MacroBlock decoding functions                                              */
/******************************************************************************/
/* 8.1.1.5, p(1)02-(1)03 */
/* We only need to store 3 flags, but math with 4 is easier */
#define GET_CBPCY(table, bits)                                      \
    predicted_cbpcy = get_vlc2(gb, table, bits, 2);             \
    cbpcy[0] = (p_cbpcy[-1] == p_cbpcy[2])                          \
         ? previous_cbpcy[1] : p_cbpcy[+2];                         \
    cbpcy[0] ^= ((predicted_cbpcy>>5)&0x01);                        \
    cbpcy[1] = (p_cbpcy[2] == p_cbpcy[3]) ? cbpcy[0] : p_cbpcy[3];  \
    cbpcy[1] ^= ((predicted_cbpcy>>4)&0x01);                        \
    cbpcy[2] = (previous_cbpcy[1] == cbpcy[0])                      \
         ? previous_cbpcy[3] : cbpcy[0];                            \
    cbpcy[2] ^= ((predicted_cbpcy>>3)&0x01);                        \
    cbpcy[3] = (cbpcy[1] == cbpcy[0]) ? cbpcy[2] : cbpcy[1];        \
    cbpcy[3] ^= ((predicted_cbpcy>>2)&0x01);
     
/* 8.1, p100 */
static int standard_decode_i_mbs(VC9Context *v)
{
    GetBitContext *gb = &v->s.gb;
    MpegEncContext *s = &v->s;
    int current_mb = 0; /* MB/Block Position info */
    /* FIXME: better to use a pointer than using (x<<4) */
    uint8_t cbpcy[4], previous_cbpcy[4], predicted_cbpcy,
        *p_cbpcy /* Pointer to skip some math */;

    /* Reset CBPCY predictors */
    memset(v->previous_line_cbpcy, 0, s->mb_stride<<2);

    /* Select ttmb table depending on pq */
    if (v->pq < 5) v->ttmb_vlc = &vc9_ttmb_vlc[0];
    else if (v->pq < 13) v->ttmb_vlc = &vc9_ttmb_vlc[1];
    else v->ttmb_vlc = &vc9_ttmb_vlc[2];

    for (s->mb_y=0; s->mb_y<s->mb_height; s->mb_y++)
    {
        /* Init CBPCY for line */
        *((uint32_t*)previous_cbpcy) = 0x00000000;
        p_cbpcy = v->previous_line_cbpcy+4;

        for (s->mb_x=0; s->mb_x<s->mb_width; s->mb_x++, p_cbpcy += 4)
        {
            /* Get CBPCY */
            GET_CBPCY(ff_msmp4_mb_i_vlc.table, MB_INTRA_VLC_BITS);

            s->ac_pred = get_bits(gb, 1);

            /* TODO: Decode blocks from that mb wrt cbpcy */

            /* Update for next block */
#if TRACE > 2
            av_log(s->avctx, AV_LOG_DEBUG, "Block %4i: p_cbpcy=%i%i%i%i, previous_cbpcy=%i%i%i%i,"
                   " cbpcy=%i%i%i%i\n", current_mb,
                   p_cbpcy[0], p_cbpcy[1], p_cbpcy[2], p_cbpcy[3],
                   previous_cbpcy[0], previous_cbpcy[1], previous_cbpcy[2], previous_cbpcy[3],
                   cbpcy[0], cbpcy[1], cbpcy[2], cbpcy[3]);
#endif
            *((uint32_t*)p_cbpcy) = *((uint32_t*)previous_cbpcy);
            *((uint32_t*)previous_cbpcy) = *((uint32_t*)cbpcy);
            current_mb++;
        }
    }
    return 0;
}

#define GET_MQUANT()                                           \
  if (v->dquantfrm)                                            \
  {                                                            \
    if (v->dqprofile == DQPROFILE_ALL_MBS)                     \
    {                                                          \
      if (v->dqbilevel)                                        \
      {                                                        \
        mquant = (get_bits(gb, 1)) ? v->pq : v->altpq;         \
      }                                                        \
      else                                                     \
      {                                                        \
        mqdiff = get_bits(gb, 3);                              \
        if (mqdiff != 7) mquant = v->pq + mqdiff;              \
        else mquant = get_bits(gb, 5);                         \
      }                                                        \
    }                                                          \
  }

/* MVDATA decoding from 8.3.5.2, p(1)20 */
#define GET_MVDATA(_dmv_x, _dmv_y)                                  \
  index = 1 + get_vlc2(gb, vc9_mv_diff_vlc[s->mv_table_index].table,\
                       VC9_MV_DIFF_VLC_BITS, 2);                    \
  if (index > 36)                                                   \
  {                                                                 \
    mb_has_coeffs = 1;                                              \
    index -= 37;                                                    \
  }                                                                 \
  else mb_has_coeffs = 0;                                           \
  s->mb_intra = 0;                                                  \
  if (!index) { _dmv_x = _dmv_y = 0; }                              \
  else if (index == 35)                                             \
  {                                                                 \
    _dmv_x = get_bits(gb, k_x);                                 \
    _dmv_y = get_bits(gb, k_y);                                 \
    s->mb_intra = 1;                                                \
  }                                                                 \
  else                                                              \
  {                                                                 \
    index1 = index%6;                                               \
    if (hpel_flag && index1 == 5) val = 1;                          \
    else                          val = 0;                          \
    val = get_bits(gb, size_table[index1] - val);               \
    sign = 0 - (val&1);                                             \
    _dmv_x = (sign ^ ((val>>1) + offset_table[index1])) - sign;     \
                                                                    \
    index1 = index/6;                                               \
    if (hpel_flag && index1 == 5) val = 1;                          \
    else                          val = 0;                          \
    val = get_bits(gb, size_table[index1] - val);               \
    sign = 0 - (val&1);                                             \
    _dmv_y = (sign ^ ((val>>1) + offset_table[index1])) - sign;     \
  }

/* 8.1, p(1)15 */
static int decode_p_mbs(VC9Context *v)
{
    MpegEncContext *s = &v->s;
    GetBitContext *gb = &v->s.gb;
    int current_mb = 0, i; /* MB/Block Position info */
    uint8_t cbpcy[4], previous_cbpcy[4], predicted_cbpcy,
        *p_cbpcy /* Pointer to skip some math */;
    int hybrid_pred; /* Prediction types */
    int mv_mode_bit = 0; 
    int mqdiff, mquant; /* MB quantization */
    int ttmb; /* MB Transform type */

    static const int size_table[6] = { 0, 2, 3, 4, 5, 8 },
        offset_table[6] = { 0, 1, 3, 7, 15, 31 };
        int mb_has_coeffs = 1; /* last_flag */
    int dmv_x, dmv_y; /* Differential MV components */
    int k_x, k_y; /* Long MV fixed bitlength */
    int hpel_flag; /* Some MB properties */
    int index, index1; /* LUT indices */
    int val, sign; /* MVDATA temp values */

    /* Select ttmb table depending on pq */
    if (v->pq < 5) v->ttmb_vlc = &vc9_ttmb_vlc[0];
    else if (v->pq < 13) v->ttmb_vlc = &vc9_ttmb_vlc[1];
    else v->ttmb_vlc = &vc9_ttmb_vlc[2];

    /* Select proper long MV range */
    switch (v->mvrange)
    {
    case 1: k_x = 10; k_y = 9; break;
    case 2: k_x = 12; k_y = 10; break;
    case 3: k_x = 13; k_y = 11; break;
    default: /*case 0 too */ k_x = 9; k_y = 8; break;
    }

    hpel_flag = v->mv_mode & 1; //MV_PMODE is HPEL
    k_x -= hpel_flag;
    k_y -= hpel_flag;

    /* Reset CBPCY predictors */
    memset(v->previous_line_cbpcy, 0, s->mb_stride<<2);

    for (s->mb_y=0; s->mb_y<s->mb_height; s->mb_y++)
    {
        /* Init CBPCY for line */
        *((uint32_t*)previous_cbpcy) = 0x00000000;
        p_cbpcy = v->previous_line_cbpcy+4;

        for (s->mb_x=0; s->mb_x<s->mb_width; s->mb_x++, p_cbpcy += 4)
        {
            if (v->mv_type_mb_plane.is_raw)
                v->mv_type_mb_plane.data[current_mb] = get_bits(gb, 1);
            if (v->skip_mb_plane.is_raw)
                v->skip_mb_plane.data[current_mb] = get_bits(gb, 1);
            if (!mv_mode_bit) /* 1MV mode */
            {
                if (!v->skip_mb_plane.data[current_mb])
                {
                    GET_MVDATA(dmv_x, dmv_y);

                    /* hybrid mv pred, 8.3.5.3.4 */
                    if (v->mv_mode == MV_PMODE_1MV ||
                        v->mv_mode == MV_PMODE_MIXED_MV)
                        hybrid_pred = get_bits(gb, 1);
                    if (s->mb_intra && !mb_has_coeffs)
                    {
                        GET_MQUANT();
                        s->ac_pred = get_bits(gb, 1);
                    }
                    else if (mb_has_coeffs)
                    {
                        if (s->mb_intra) s->ac_pred = get_bits(gb, 1);
                        predicted_cbpcy = get_vlc2(gb, v->cbpcy_vlc->table, VC9_CBPCY_P_VLC_BITS, 2);
                        cbpcy[0] = (p_cbpcy[-1] == p_cbpcy[2]) ? previous_cbpcy[1] : p_cbpcy[2];
                        cbpcy[0] ^= ((predicted_cbpcy>>5)&0x01);
                        cbpcy[1] = (p_cbpcy[2] == p_cbpcy[3]) ? cbpcy[0] : p_cbpcy[3];
                        cbpcy[1] ^= ((predicted_cbpcy>>4)&0x01);
                        cbpcy[2] = (previous_cbpcy[1] == cbpcy[0]) ? previous_cbpcy[3] : cbpcy[0];
                        cbpcy[2] ^= ((predicted_cbpcy>>3)&0x01);
                        cbpcy[3] = (cbpcy[1] == cbpcy[0]) ? cbpcy[2] : cbpcy[1];
                        cbpcy[3] ^= ((predicted_cbpcy>>2)&0x01);
                        //GET_CBPCY(v->cbpcy_vlc->table, VC9_CBPCY_P_VLC_BITS);

                        GET_MQUANT();
                    }
                    if (!v->ttmbf)
                        ttmb = get_vlc2(gb, v->ttmb_vlc->table,
                                            VC9_TTMB_VLC_BITS, 12);
                    /* TODO: decode blocks from that mb wrt cbpcy */
                }
                else //Skipped
                {
                    /* hybrid mv pred, 8.3.5.3.4 */
                    if (v->mv_mode == MV_PMODE_1MV ||
                        v->mv_mode == MV_PMODE_MIXED_MV)
                        hybrid_pred = get_bits(gb, 1);
                }
            } //1MV mode
            else //4MV mode
            {
              if (!v->skip_mb_plane.data[current_mb] /* unskipped MB */)
                {
                    /* Get CBPCY */
                    GET_CBPCY(v->cbpcy_vlc->table, VC9_CBPCY_P_VLC_BITS);
                    for (i=0; i<4; i++) //For all 4 Y blocks
                    {
                        if (cbpcy[i] /* cbpcy set for this block */)
                        {
                            GET_MVDATA(dmv_x, dmv_y);
                        }
                        if (v->mv_mode == MV_PMODE_MIXED_MV /* Hybrid pred */)
                            hybrid_pred = get_bits(gb, 1);
                        GET_MQUANT();
                        if (s->mb_intra /* One of the 4 blocks is intra */ &&
                            index /* non-zero pred for that block */)
                            s->ac_pred = get_bits(gb, 1);
                        if (!v->ttmbf)
                            ttmb = get_vlc2(gb, v->ttmb_vlc->table,
                                            VC9_TTMB_VLC_BITS, 12);
            
                        /* TODO: Process blocks wrt cbpcy */
            
                    }
                }
                else //Skipped MB
                {
                    for (i=0; i<4; i++) //All 4 Y blocks
                    {
                        if (v->mv_mode == MV_PMODE_MIXED_MV /* Hybrid pred */)
                            hybrid_pred = get_bits(gb, 1);
                        
                        /* TODO: do something */
                    }
                }
            }

            /* Update for next block */
#if TRACE > 2
            av_log(s->avctx, AV_LOG_DEBUG, "Block %4i: p_cbpcy=%i%i%i%i, previous_cbpcy=%i%i%i%i,"
                   " cbpcy=%i%i%i%i\n", current_mb,
                   p_cbpcy[0], p_cbpcy[1], p_cbpcy[2], p_cbpcy[3],
                   previous_cbpcy[0], previous_cbpcy[1], previous_cbpcy[2], previous_cbpcy[3],
                   cbpcy[0], cbpcy[1], cbpcy[2], cbpcy[3]);
#endif
            *((uint32_t*)p_cbpcy) = *((uint32_t*)previous_cbpcy);
            *((uint32_t*)previous_cbpcy) = *((uint32_t*)cbpcy);
            current_mb++;
        }
    }
    return 0;
}

static int decode_b_mbs(VC9Context *v)
{
    MpegEncContext *s = &v->s;
    GetBitContext *gb = &v->s.gb;
    int current_mb = 0, i /* MB / B postion information */;
    int b_mv_type = BMV_TYPE_BACKWARD;
    int mquant, mqdiff; /* MB quant stuff */
    int ttmb; /* MacroBlock transform type */
    
    static const int size_table[6] = { 0, 2, 3, 4, 5, 8 },
        offset_table[6] = { 0, 1, 3, 7, 15, 31 };
    int mb_has_coeffs = 1; /* last_flag */
    int dmv1_x, dmv1_y, dmv2_x, dmv2_y; /* Differential MV components */
    int k_x, k_y; /* Long MV fixed bitlength */
    int hpel_flag; /* Some MB properties */
    int index, index1; /* LUT indices */
    int val, sign; /* MVDATA temp values */
    
    /* Select proper long MV range */
    switch (v->mvrange)
    {
    case 1: k_x = 10; k_y = 9; break;
    case 2: k_x = 12; k_y = 10; break;
    case 3: k_x = 13; k_y = 11; break;
    default: /*case 0 too */ k_x = 9; k_y = 8; break;
    }
    hpel_flag = v->mv_mode & 1; //MV_PMODE is HPEL
    k_x -= hpel_flag;
    k_y -= hpel_flag;

    /* Select ttmb table depending on pq */
    if (v->pq < 5) v->ttmb_vlc = &vc9_ttmb_vlc[0];
    else if (v->pq < 13) v->ttmb_vlc = &vc9_ttmb_vlc[1];
    else v->ttmb_vlc = &vc9_ttmb_vlc[2];

    for (s->mb_y=0; s->mb_y<s->mb_height; s->mb_y++)
    {
        for (s->mb_x=0; s->mb_x<s->mb_width; s->mb_x++)
        {
            if (v->direct_mb_plane.is_raw)
                v->direct_mb_plane.data[current_mb] = get_bits(gb, 1);
            if (v->skip_mb_plane.is_raw)
                v->skip_mb_plane.data[current_mb] = get_bits(gb, 1);
            
            if (!v->direct_mb_plane.data[current_mb])
            {
                if (v->skip_mb_plane.data[current_mb])
                {
                    b_mv_type = decode012(gb);
                    if (v->bfraction > 420 /*1/2*/ &&
                        b_mv_type < 3) b_mv_type = 1-b_mv_type;
                }
                else
                { 
                    /* FIXME getting tired commenting */
                    GET_MVDATA(dmv1_x, dmv1_y);
                    if (!s->mb_intra /* b_mv1 tells not intra */)
                    {
                        /* FIXME: actually read it */
                        b_mv_type = decode012(gb);
                        if (v->bfraction > 420 /*1/2*/ &&
                            b_mv_type < 3) b_mv_type = 1-b_mv_type;
                    }
                }
            }
            if (!v->skip_mb_plane.data[current_mb])
            {
                if (mb_has_coeffs /* BMV1 == "last" */)
                {
                    GET_MQUANT();
                    if (s->mb_intra /* intra mb */)
                        s->ac_pred = get_bits(gb, 1);
                }
                else
                {
                    /* if bmv1 tells MVs are interpolated */
                    if (b_mv_type == BMV_TYPE_INTERPOLATED)
                    {
                        GET_MVDATA(dmv2_x, dmv2_y);
                    }
                    /* GET_MVDATA has reset some stuff */
                    if (mb_has_coeffs /* b_mv2 == "last" */)
                    {
                        if (s->mb_intra /* intra_mb */)
                            s->ac_pred = get_bits(gb, 1);
                        GET_MQUANT();
                    }
                }
            }
            //End1
            if (v->ttmbf)
                ttmb = get_vlc2(gb, v->ttmb_vlc->table,
                                   VC9_TTMB_VLC_BITS, 12);

            //End2
            for (i=0; i<6; i++)
            {
                /* FIXME: process the block */
            }

            current_mb++;
        }
    }
    return 0;
}

#if HAS_ADVANCED_PROFILE
static int advanced_decode_i_mbs(VC9Context *v)
{
    MpegEncContext *s = &v->s;
    GetBitContext *gb = &v->s.gb;
    int mqdiff, mquant, current_mb = 0, over_flags_mb = 0;

    for (s->mb_y=0; s->mb_y<s->mb_height; s->mb_y++)
    {
        for (s->mb_x=0; s->mb_x<s->mb_width; s->mb_x++)
        {
            if (v->ac_pred_plane.is_raw)
                s->ac_pred = get_bits(gb, 1);
            else
                s->ac_pred = v->ac_pred_plane.data[current_mb];
            if (v->condover == 3 && v->over_flags_plane.is_raw)
                over_flags_mb = get_bits(gb, 1);
            GET_MQUANT();

            /* TODO: lots */
        }
        current_mb++;
    }
    return 0;
}
#endif

static int vc9_decode_init(AVCodecContext *avctx)
{
    VC9Context *v = avctx->priv_data;
    MpegEncContext *s = &v->s;
    GetBitContext gb;

    if (!avctx->extradata_size || !avctx->extradata) return -1;
    avctx->pix_fmt = PIX_FMT_YUV420P;
    v->s.avctx = avctx;

    if(ff_h263_decode_init(avctx) < 0)
        return -1;
    if (vc9_init_common(v) < 0) return -1;

    avctx->coded_width = avctx->width;
    avctx->coded_height = avctx->height;
    if (avctx->codec_id == CODEC_ID_WMV3)
    {
        int count = 0;

        // looks like WMV3 has a sequence header stored in the extradata
        // advanced sequence header may be before the first frame
        // the last byte of the extradata is a version number, 1 for the
        // samples we can decode

        init_get_bits(&gb, avctx->extradata, avctx->extradata_size);
        
        decode_sequence_header(avctx, &gb);

        count = avctx->extradata_size*8 - get_bits_count(&gb);
        if (count>0)
        {
            av_log(avctx, AV_LOG_INFO, "Extra data: %i bits left, value: %X\n",
                   count, get_bits(&gb, count));
        }
        else
        {
            av_log(avctx, AV_LOG_INFO, "Read %i bits in overflow\n", -count);
        }
    }
    avctx->has_b_frames= !!(avctx->max_b_frames);

    s->mb_width = (avctx->coded_width+15)>>4;
    s->mb_height = (avctx->coded_height+15)>>4;

    /* Allocate mb bitplanes */
    if (alloc_bitplane(&v->mv_type_mb_plane, s->mb_width, s->mb_height) < 0)
        return -1;
    if (alloc_bitplane(&v->mv_type_mb_plane, s->mb_width, s->mb_height) < 0)
        return -1;
    if (alloc_bitplane(&v->skip_mb_plane, s->mb_width, s->mb_height) < 0)
        return -1;
    if (alloc_bitplane(&v->direct_mb_plane, s->mb_width, s->mb_height) < 0)
        return -1;

    /* For predictors */
    v->previous_line_cbpcy = (uint8_t *)av_malloc(s->mb_stride*4);
    if (!v->previous_line_cbpcy) return -1;

#if HAS_ADVANCED_PROFILE
    if (v->profile > PROFILE_MAIN)
    {
        if (alloc_bitplane(&v->over_flags_plane, s->mb_width, s->mb_height) < 0)
            return -1;
        if (alloc_bitplane(&v->ac_pred_plane, s->mb_width, s->mb_height) < 0)
            return -1;
    }
#endif

    return 0;
    }

static int vc9_decode_frame(AVCodecContext *avctx,
                            void *data, int *data_size,
                            uint8_t *buf, int buf_size)
{
    VC9Context *v = avctx->priv_data;
    MpegEncContext *s = &v->s;
    int ret = FRAME_SKIPED, len, start_code;
    AVFrame *pict = data;
    uint8_t *tmp_buf;
    v->s.avctx = avctx;

    //buf_size = 0 -> last frame
    if (!buf_size) return 0;

    len = avpicture_get_size(avctx->pix_fmt, avctx->width,
                             avctx->height);
    tmp_buf = (uint8_t *)av_mallocz(len);
    avpicture_fill((AVPicture *)pict, tmp_buf, avctx->pix_fmt,
                   avctx->width, avctx->height);

    if (avctx->codec_id == CODEC_ID_VC9)
    {
#if 0
	// search for IDU's
	// FIXME
	uint32_t scp = 0;
	int scs = 0, i = 0;

	while (i < buf_size)
	{
	    for (; i < buf_size && scp != 0x000001; i++)
		scp = ((scp<<8)|buf[i])&0xffffff;

	    if (scp != 0x000001)
		break; // eof ?
	
	    scs = buf[i++];	

	    init_get_bits(gb, buf+i, (buf_size-i)*8);
	
	    switch(scs)
	    {
            case 0x0A: //Sequence End Code
                return 0;
            case 0x0B: //Slice Start Code
                av_log(avctx, AV_LOG_ERROR, "Slice coding not supported\n");
                return -1;
            case 0x0C: //Field start code
                av_log(avctx, AV_LOG_ERROR, "Interlaced coding not supported\n");
                return -1;
            case 0x0D: //Frame start code
                break;
            case 0x0E: //Entry point Start Code
                if (v->profile <= MAIN_PROFILE)
                    av_log(avctx, AV_LOG_ERROR,
                           "Found an entry point in profile %i\n", v->profile);
                advanced_entry_point_process(avctx, gb);
                break;
            case 0x0F: //Sequence header Start Code
                decode_sequence_header(avctx, gb);
                break;
            default:
                av_log(avctx, AV_LOG_ERROR,
                       "Unsupported IDU suffix %lX\n", scs);
            }
	    
	    i += get_bits_count(gb)*8;
	}
#else
	av_abort();
#endif
    }
    else
        init_get_bits(&v->s.gb, buf, buf_size*8);

    s->flags= avctx->flags;
    s->flags2= avctx->flags2;

    /* no supplementary picture */
    if (buf_size == 0) {
        /* special case for last picture */
        if (s->low_delay==0 && s->next_picture_ptr) {
            *pict= *(AVFrame*)s->next_picture_ptr;
            s->next_picture_ptr= NULL;

            *data_size = sizeof(AVFrame);
        }

        return 0;
    }

    //No IDU - we mimic ff_h263_decode_frame
    s->bitstream_buffer_size=0;
        
    if (!s->context_initialized) {
        if (MPV_common_init(s) < 0) //we need the idct permutaton for reading a custom matrix
            return -1;
    }
    
    //we need to set current_picture_ptr before reading the header, otherwise we cant store anyting im there
    if(s->current_picture_ptr==NULL || s->current_picture_ptr->data[0]){
        s->current_picture_ptr= &s->picture[ff_find_unused_picture(s, 0)];
    }
#if HAS_ADVANCED_PROFILE
    if (v->profile > PROFILE_MAIN)
        ret= advanced_decode_picture_primary_header(v);
    else
#endif
        ret= standard_decode_picture_primary_header(v);
    if (ret == FRAME_SKIPED) return buf_size;
    /* skip if the header was thrashed */
    if (ret < 0){
        av_log(s->avctx, AV_LOG_ERROR, "header damaged\n");
        return -1;
    }

    //No bug workaround yet, no DCT conformance

    //WMV9 does have resized images
    if (v->profile <= PROFILE_MAIN && v->multires){
        //Parse context stuff in here, don't know how appliable it is
    }
    //Not sure about context initialization

    // for hurry_up==5
    s->current_picture.pict_type= s->pict_type;
    s->current_picture.key_frame= s->pict_type == I_TYPE;

    /* skip b frames if we dont have reference frames */
    if(s->last_picture_ptr==NULL && (s->pict_type==B_TYPE || s->dropable))
        return buf_size; //FIXME simulating all buffer consumed
    /* skip b frames if we are in a hurry */
    if(avctx->hurry_up && s->pict_type==B_TYPE)
        return buf_size; //FIXME simulating all buffer consumed
    /* skip everything if we are in a hurry>=5 */
    if(avctx->hurry_up>=5)
        return buf_size; //FIXME simulating all buffer consumed
    
    if(s->next_p_frame_damaged){
        if(s->pict_type==B_TYPE)
            return buf_size; //FIXME simulating all buffer consumed
        else
            s->next_p_frame_damaged=0;
    }

    if(MPV_frame_start(s, avctx) < 0)
        return -1;

    ff_er_frame_start(s);

    //wmv9 may or may not have skip bits
#if HAS_ADVANCED_PROFILE
    if (v->profile > PROFILE_MAIN)
        ret= advanced_decode_picture_secondary_header(v);
    else
#endif
        ret = standard_decode_picture_secondary_header(v);
    if (ret<0) return FRAME_SKIPED; //FIXME Non fatal for now

    //We consider the image coded in only one slice
#if HAS_ADVANCED_PROFILE
    if (v->profile > PROFILE_MAIN)
    {
        switch(s->pict_type)
        {
            case I_TYPE: ret = advanced_decode_i_mbs(v); break;
            case P_TYPE: ret = decode_p_mbs(v); break;
            case B_TYPE:
            case BI_TYPE: ret = decode_b_mbs(v); break;
            default: ret = FRAME_SKIPED;
        }
        if (ret == FRAME_SKIPED) return buf_size; //We ignore for now failures
    }
    else
#endif
    {
        switch(s->pict_type)
        {
            case I_TYPE: ret = standard_decode_i_mbs(v); break;
            case P_TYPE: ret = decode_p_mbs(v); break;
            case B_TYPE:
            case BI_TYPE: ret = decode_b_mbs(v); break;
            default: ret = FRAME_SKIPED;
        }
        if (ret == FRAME_SKIPED) return buf_size;
    }

    ff_er_frame_end(s);

    MPV_frame_end(s);

    assert(s->current_picture.pict_type == s->current_picture_ptr->pict_type);
    assert(s->current_picture.pict_type == s->pict_type);
    if(s->pict_type==B_TYPE || s->low_delay){
        *pict= *(AVFrame*)&s->current_picture;
        ff_print_debug_info(s, pict);
    } else {
        *pict= *(AVFrame*)&s->last_picture;
        if(pict)
            ff_print_debug_info(s, pict);
    }

    /* Return the Picture timestamp as the frame number */
    /* we substract 1 because it is added on utils.c    */
    avctx->frame_number = s->picture_number - 1;

    /* dont output the last pic after seeking */
    if(s->last_picture_ptr || s->low_delay)
        *data_size = sizeof(AVFrame);

    av_log(avctx, AV_LOG_DEBUG, "Consumed %i/%i bits\n",
           get_bits_count(&s->gb), buf_size*8);

    /* Fake consumption of all data */
    *data_size = len;
    return buf_size; //Number of bytes consumed
}

static int vc9_decode_end(AVCodecContext *avctx)
{
    VC9Context *v = avctx->priv_data;

#if HAS_ADVANCED_PROFILE
    av_freep(&v->hrd_rate);
    av_freep(&v->hrd_buffer);
#endif
    MPV_common_end(&v->s);
    free_bitplane(&v->mv_type_mb_plane);
    free_bitplane(&v->skip_mb_plane);
    free_bitplane(&v->direct_mb_plane);
    return 0;
}

AVCodec vc9_decoder = {
    "vc9",
    CODEC_TYPE_VIDEO,
    CODEC_ID_VC9,
    sizeof(VC9Context),
    vc9_decode_init,
    NULL,
    vc9_decode_end,
    vc9_decode_frame,
    CODEC_CAP_DELAY,
    NULL
};

AVCodec wmv3_decoder = {
    "wmv3",
    CODEC_TYPE_VIDEO,
    CODEC_ID_WMV3,
    sizeof(VC9Context),
    vc9_decode_init,
    NULL,
    vc9_decode_end,
    vc9_decode_frame,
    CODEC_CAP_DELAY,
    NULL
};
