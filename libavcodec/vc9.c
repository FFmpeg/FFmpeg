/*
 * VC-9 and WMV3 decoder
 * Copyright (c) 2005 Anonymous
 * Copyright (c) 2005 Alex Beregszaszi
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

/* Some inhibiting stuff */
#define HAS_ADVANCED_PROFILE   1
#define TRACE                  1

#if TRACE
#  define INIT_VLC(vlc, nb_bits, nb_codes, bits, bits_wrap, bits_size, \
                   codes, codes_wrap, codes_size, use_static)          \
  if (init_vlc(vlc, nb_bits, nb_codes, bits, bits_wrap, bits_size,     \
               codes, codes_wrap, codes_size, use_static) < 0)         \
  {                                                                    \
    av_log(v->avctx, AV_LOG_ERROR, "Error for " # vlc " (%i)\n", i);   \
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


/* Start Codes */
#define SEQ_SC      0x00000010F /* Sequence Start Code */
#define SEQ_EC      0x00000000A /* Sequence End code */
#define SEQ_HDR     /* Sequence Header */
#define ENTRY_SC    0x00000010E /* Entry Point Start Code */
#define ENTRY_HDR /* Entry Point Header */
#define FRM_SC      0x00000010D /* Frame Start Code */
#define FRM_DAT /* Frame Data (includes a Frame Header) */
#define FLD_SC      0x00000010C /* Field Start Code */
#define FLD1_DAT /* Field 1 Data (includes a Frame Header) */
#define FLD2_DAT /* Field 2 Data (includes a Field Header) */
#define SLC_SC      0x00000010B /* Slice Start Code */
#define SLC_HDR /* Slice Header */
#define SLC_DAT /* Slice Data (FrH or FiH possible) */

/* MV P modes */
#define MV_PMODE_1MV_HPEL_BILIN   0
#define MV_PMODE_1MV              1
#define MV_PMODE_1MV_HPEL         2
#define MV_PMODE_MIXED_MV         3
#define MV_PMODE_INTENSITY_COMP   4

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
/* Could be optimized, one table only needs 8 bits */
#define VC9_TTMB_VLC_BITS 12
static VLC vc9_ttmb_vlc[3];
#define VC9_MV_DIFF_VLC_BITS 15
static VLC vc9_mv_diff_vlc[4];
#define VC9_CBPCY_I_VLC_BITS 13
static VLC vc9_cbpcy_i_vlc;
#define VC9_CBPCY_P_VLC_BITS 14
static VLC vc9_cbpcy_p_vlc[4];
#define VC9_4MV_BLOCK_PATTERN_VLC_BITS 6
static VLC vc9_4mv_block_pattern_vlc[4];
#define VC9_LUMA_DC_VLC_BITS 26
static VLC vc9_luma_dc_vlc[2];

typedef struct VC9Context{
  /* No MpegEnc context, might be good to use it */
  GetBitContext gb;
  AVCodecContext *avctx;

  /***************************/
  /* Sequence Header         */
  /***************************/
  /* Simple/Main Profile */
  int res_sm; //reserved, 2b
  int res_x8; //reserved
  int multires; //frame-level RESPIC syntax element present
  int res_fasttx; //always 1
  int res_transtab; //always 0
  int syncmarker; //Sync markers presents
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
  int dquant; //Q varies with MBs, 2bits (not in Simple)
  int vstransform; //variable-size transform46

  int overlap; //overlapped transforms in use
  int quantizer_mode; //2, quantizer mode used for sequence, see QUANT_*
  int finterpflag; //INTERPFRM present


  /*****************************/
  /* Frame decoding            */
  /*****************************/
  /* All profiles */
  uint8_t mv_mode, mv_mode2; /* MV coding mode */
  uint8_t pict_type; /* Picture type, mapped on MPEG types */
  uint8_t pq, altpq; /* Quantizers */
  uint8_t dquantfrm, dqprofile, dqsbedge, dqbilevel; /* pquant parameters */
  int width_mb, height_mb;
  int tile; /* 3x2 if (width_mb%3) else 2x3 */
  int transacfrm2, transacfrm, transacdctab; //1bit elements
  uint8_t ttmbf, ttfrm; /* Transform type */
  uint8_t lumscale, lumshift; /* Luma compensation parameters */
  int16_t bfraction; /* Relative position % anchors=> how to scale MVs */
  uint8_t halfpq; /* Uniform quant over image and qp+.5 */
  uint8_t respic;
  /* Ranges:
   * 0 -> [-64n 63.f] x [-32, 31.f]
   * 1 -> [-128, 127.f] x [-64, 63.f]
   * 2 -> [-512, 511.f] x [-128, 127.f]
   * 3 -> [-1024, 1023.f] x [-256, 255.f]
   */
  uint8_t mvrange;
  uint8_t pquantizer;
  VLC *cbpcy_vlc /* Current CBPCY VLC table */,
    *mv_diff_vlc /* Current MV Diff VLC table */,
    *ttmb_vlc /* Current MB Transform Type VLC table */;
  uint8_t *mv_type_mb_plane; /* bitplane for mv_type == "raw" */
  uint8_t *skip_mb_plane, /* bitplane for skipped MBs */
    *direct_mb_plane; /* bitplane for "direct" MBs */

  /* S/M only ? */
  uint8_t rangeredfrm, interpfrm;

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
  uint8_t rndctrl;
  uint8_t uvsamp;
  uint8_t postproc;
  int hrd_num_leaky_buckets;
  uint8_t bit_rate_exponent;
  uint8_t buffer_size_exponent;
  uint8_t *ac_pred_plane;
  uint8_t *over_flags_plane;
  uint16_t *hrd_rate, *hrd_buffer;
#endif
} VC9Context;

/* FIXME Slow and ugly */
static int get_prefix(GetBitContext *gb, int stop, int len)
{
  int i = 0, tmp = !stop;

  while (i != len && tmp != stop)
  {
    tmp = get_bits(gb, 1);
    i++;
  }
  return i;
}

static int init_common(VC9Context *v)
{
    static int done = 0;
    int i;

    v->mv_type_mb_plane = v->direct_mb_plane = v->skip_mb_plane = NULL;
    v->pq = -1;
#if HAS_ADVANCED_PROFILE
    v->ac_pred_plane = v->over_flags_plane = NULL;
    v->hrd_rate = v->hrd_buffer = NULL;
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
        INIT_VLC(&vc9_cbpcy_i_vlc, VC9_CBPCY_I_VLC_BITS, 64,
                 vc9_cbpcy_i_bits, 1, 1,
                 vc9_cbpcy_i_codes, 2, 2, 1);
        INIT_VLC(&vc9_imode_vlc, VC9_IMODE_VLC_BITS, 7,
                 vc9_imode_bits, 1, 1,
                 vc9_imode_codes, 1, 1, 1);
        for(i=0; i<3; i++)
        {
            INIT_VLC(&vc9_4mv_block_pattern_vlc[i], VC9_4MV_BLOCK_PATTERN_VLC_BITS, 16,
                     vc9_4mv_block_pattern_bits[i], 1, 1,
                     vc9_4mv_block_pattern_codes[i], 1, 1, 1);
            INIT_VLC(&vc9_cbpcy_p_vlc[i], VC9_CBPCY_P_VLC_BITS, 64,
                     vc9_cbpcy_p_bits[i], 1, 1,
                     vc9_cbpcy_p_codes[i], 2, 2, 1);
        }
        for (i=0; i<2; i++)
        {
            INIT_VLC(&vc9_mv_diff_vlc[i], VC9_MV_DIFF_VLC_BITS, 73,
                     vc9_mv_diff_bits[i], 1, 1,
                     vc9_mv_diff_codes[i], 2, 2, 1);
            INIT_VLC(&vc9_luma_dc_vlc[i], VC9_LUMA_DC_VLC_BITS, 26,
                     vc9_luma_dc_bits[i], 1, 1,
                     vc9_luma_dc_codes[i], 4, 4, 1);
            INIT_VLC(&vc9_ttmb_vlc[i], VC9_TTMB_VLC_BITS, 16,
                     vc9_ttmb_bits[i], 1, 1,
                     vc9_ttmb_codes[i], 2, 2, 1);
        }
    }

    return 0;
}

#if HAS_ADVANCED_PROFILE
static int decode_hrd(VC9Context *v, GetBitContext *gb)
{
    int i, num;

    num = get_bits(gb, 5);

    if (v->hrd_rate || num != v->hrd_num_leaky_buckets)
    {
        av_free(v->hrd_rate);
        v->hrd_rate = NULL;
    }
    if (!v->hrd_rate) v->hrd_rate = av_malloc(num);
    if (!v->hrd_rate) return -1;

    if (v->hrd_buffer || num != v->hrd_num_leaky_buckets)
    {
        av_free(v->hrd_buffer);
        v->hrd_buffer = NULL;
    }
    if (!v->hrd_buffer) v->hrd_buffer = av_malloc(num);
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
            av_log(v, AV_LOG_ERROR, "HDR Rates aren't strictly increasing:"
                   "%i vs %i\n", v->hrd_rate[i-1], v->hrd_rate[i]);
            return -1;
        }
        v->hrd_buffer[i] = get_bits(gb, 16);
        if (i && v->hrd_buffer[i-1]<v->hrd_buffer[i])
        {
            av_log(v, AV_LOG_ERROR, "HDR Buffers aren't decreasing:"
                   "%i vs %i\n", v->hrd_buffer[i-1], v->hrd_buffer[i]);
            return -1;
        }
    }
    return 0;
}

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

    if (get_bits(gb, 1) /* pic_size_flag */)
    {
        avctx->coded_width = get_bits(gb, 12);
        avctx->coded_height = get_bits(gb, 12);
        if ( get_bits(gb, 1) /* disp_size_flag */)
        {
            avctx->width = get_bits(gb, 14);
            avctx->height = get_bits(gb, 14);
        }

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

    if ( get_bits(gb, 1) /* framerateflag */)
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
    v->loopfilter = get_bits(gb, 1); //common

#if HAS_ADVANCED_PROFILE
    if (v->profile <= PROFILE_MAIN)
#endif
    {
        v->res_x8 = get_bits(gb, 1); //reserved
        if (v->res_x8)
        {
            av_log(avctx, AV_LOG_ERROR,
                   "1 for reserved RES_X8 is forbidden\n");
            return -1;
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
        v->syncmarker = get_bits(gb, 1);
        v->rangered = get_bits(gb, 1);
    }

    avctx->max_b_frames = get_bits(gb, 3); //common
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
               v->rangered, v->vstransform, v->overlap, v->syncmarker,
               v->dquant, v->quantizer_mode, avctx->max_b_frames
               );
#endif
    }
#if HAS_ADVANCED_PROFILE
    else decode_advanced_sequence_header(avctx, gb);
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
/* Bitplane decoding                                                          */
/******************************************************************************/
#define IMODE_RAW     0
#define IMODE_NORM2   1
#define IMODE_DIFF2   2
#define IMODE_NORM6   3
#define IMODE_DIFF6   4
#define IMODE_ROWSKIP 5
#define IMODE_COLSKIP 6

//FIXME optimize
static int bitplane_decoding(uint8_t* plane, int width, int height, VC9Context *v)
{
    int imode, x, y, code;
    uint8_t invert, *planep = plane;

    invert = get_bits(&v->gb, 1);
    imode = get_vlc2(&v->gb, vc9_imode_vlc.table, VC9_IMODE_VLC_BITS, 2);
    av_log(v->avctx, AV_LOG_DEBUG, "Bitplane: imode=%i, invert=%i\n",
           imode, invert);

    switch (imode)
    {
    case IMODE_RAW:
        for (y=0; y<height; y++)
        {
            for (x=0; x<width; x++)
                planep[x] = (-get_bits(&v->gb, 1)); //-1=0xFF
            planep += width;
        }
        break;
    case IMODE_DIFF2:
    case IMODE_NORM2:
        if ((height*width) & 1) *(++planep) = get_bits(&v->gb, 1);
        code = get_vlc2(&v->gb, vc9_norm2_vlc.table, VC9_NORM2_VLC_BITS, 2);
        *(++planep) = code&1; //lsb => left
        *(++planep) = code&2; //msb => right - this is a bitplane, so only !0 matters
        break;
    case IMODE_DIFF6:
    case IMODE_NORM6:
        av_log(v->avctx, AV_LOG_ERROR, "Imode using Norm-6 isn't supported\n");
        return -1;
        break;
    case IMODE_ROWSKIP:
        for (y=0; y<height; y++)
        {
            if (get_bits(&v->gb, 1)) //rowskip
              memset(planep, 0, width);
            else for (x=0; x<width; x++) planep[x] = get_bits(&v->gb, 1);
            planep += width;
        }
        break;
    case IMODE_COLSKIP: //Teh ugly
        for (x=0; x<width; x++)
        {
            planep = plane;
            if (get_bits(&v->gb, 1)) //colskip
            {
                for (y=0; y<height; y++)
                {
                    planep[x] = 0;
                    planep += width;
                }
            }
            else
            {
                for (y=0; y<height; y++)
                {
                    planep[x] = get_bits(&v->gb, 1);
                    planep += width;
                }
            }
        }
        break;
    default: break;
    }

    /* Applying diff operator */
    if (imode == IMODE_DIFF2 || imode == IMODE_DIFF2)
    {
        /* And what about j=0 !? */
        planep = plane + width;
        memset(plane, invert, width);
        for (y=0; y<height; y++)
        {
            planep[0] = planep[-width];
            for (x=1; x<width; x++)
            {
                if (planep[x-1] != planep[-width]) planep[x] = invert;
                else planep[x] = planep[x-1];
            }
            planep += width;
        }
    }
    else if (invert)
    {
        planep = plane;
        for (x=0; x<width*height; x++) planep[x] = !planep[x];
    }
    return 0;
}

/*****************************************************************************/
/* VOP Dquant decoding                                                       */
/*****************************************************************************/
static int vop_dquant_decoding(VC9Context *v)
{
    int pqdiff;

    //variable size
    if (v->dquant == 2)
    {
        pqdiff = get_bits(&v->gb, 3);
        if (pqdiff == 7) v->altpq = get_bits(&v->gb, 5);
        else v->altpq = v->pq + pqdiff + 1;
    }
    else
    {
        v->dquantfrm = get_bits(&v->gb, 1);
        if ( v->dquantfrm )
        {
            v->dqprofile = get_bits(&v->gb, 2);
            switch (v->dqprofile)
            {
            case DQPROFILE_SINGLE_EDGE:
            case DQPROFILE_DOUBLE_EDGES:
                v->dqsbedge = get_bits(&v->gb, 2);
                break;
            case DQPROFILE_ALL_MBS:
                v->dqbilevel = get_bits(&v->gb, 1);
            default: break; //Forbidden ?
            }
            if (!v->dqbilevel || v->dqprofile != DQPROFILE_ALL_MBS)
            {
                pqdiff = get_bits(&v->gb, 3);
                if (pqdiff == 7) v->altpq = get_bits(&v->gb, 5);
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
static int decode_bi_picture_header(VC9Context *v)
{
    /* Very particular case:
       - for S/M Profiles, decode_b_picture_header reads BF,
         bfraction then determine if this is a BI frame, calling
         this function afterwards
       - for A Profile, PTYPE already tells so and we can go
         directly there
    */
    int pqindex;

    /* Read the quantization stuff */
    pqindex = get_bits(&v->gb, 5);
    if (v->quantizer_mode == QUANT_FRAME_IMPLICIT)
        v->pq = pquant_table[0][pqindex];
    else
    {
        v->pq = pquant_table[v->quantizer_mode-1][pqindex];
    }
    if (pqindex < 9) v->halfpq = get_bits(&v->gb, 1);
    if (v->quantizer_mode == QUANT_FRAME_EXPLICIT)
        v->pquantizer = get_bits(&v->gb, 1);

    /* Read the MV type/mode */
    if (v->extended_mv == 1)
        v->mvrange = get_prefix(&v->gb, 0, 3);

    /* FIXME: what table are used in that case ? */
    v->mv_diff_vlc = &vc9_mv_diff_vlc[0];
    v->cbpcy_vlc = &vc9_cbpcy_i_vlc;

    av_log(v->avctx, AV_LOG_DEBUG, "B frame, QP=%i\n", v->pq);
    av_log(v->avctx, AV_LOG_ERROR, "BI_TYPE not supported yet\n");
    /* Epilog should be done in caller */
    return -1;
}

static int decode_b_picture_header(VC9Context *v)
{
    int pqindex;

    /* Prolog common to all frametypes should be done in caller */
    if (v->profile == PROFILE_SIMPLE)
    {
        av_log(v, AV_LOG_ERROR, "Found a B frame while in Simple Profile!\n");
        return FRAME_SKIPED;
    }

    v->bfraction = vc9_bfraction_lut[get_vlc2(&v->gb, vc9_bfraction_vlc.table,
                                              VC9_BFRACTION_VLC_BITS, 2)];
    if (v->bfraction < -1)
    {
        av_log(v, AV_LOG_ERROR, "Invalid BFRaction\n");
        return FRAME_SKIPED;
    }
    else if (!v->bfraction)
    {
        /* We actually have a BI frame */
        return decode_bi_picture_header(v);
    }

    /* Read the quantization stuff */
    pqindex = get_bits(&v->gb, 5);
    if (v->quantizer_mode == QUANT_FRAME_IMPLICIT)
        v->pq = pquant_table[0][pqindex];
    else
    {
        v->pq = pquant_table[v->quantizer_mode-1][pqindex];
    }
    if (pqindex < 9) v->halfpq = get_bits(&v->gb, 1);
    if (v->quantizer_mode == QUANT_FRAME_EXPLICIT)
        v->pquantizer = get_bits(&v->gb, 1);

    /* Read the MV type/mode */
    if (v->extended_mv == 1)
        v->mvrange = get_prefix(&v->gb, 0, 3);
    v->mv_mode = get_bits(&v->gb, 1);
    if (v->pq < 13)
    {
        if (!v->mv_mode)
        {
            v->mv_mode = get_bits(&v->gb, 2);
            if (v->mv_mode)
                av_log(v, AV_LOG_ERROR,
                       "mv_mode for lowquant B frame was %i\n", v->mv_mode);
        }
    }
    else
    {
        if (!v->mv_mode)
        {
            if (get_bits(&v->gb, 1))
                av_log(v, AV_LOG_ERROR,
                       "mv_mode for highquant B frame was %i\n", v->mv_mode);
        }
        v->mv_mode = 1-v->mv_mode; //To match (pq < 13) mapping
    }

    if (v->mv_mode == MV_PMODE_MIXED_MV)
    {
        if (bitplane_decoding( v->mv_type_mb_plane, v->width_mb,
                                   v->height_mb, v)<0)
            return -1;
    }

    //bitplane
    bitplane_decoding(v->direct_mb_plane, v->width_mb, v->height_mb, v);
    bitplane_decoding(v->skip_mb_plane, v->width_mb, v->height_mb, v);

    /* FIXME: what is actually chosen for B frames ? */
    v->mv_diff_vlc = &vc9_mv_diff_vlc[get_bits(&v->gb, 2)];
    v->cbpcy_vlc = &vc9_cbpcy_p_vlc[get_bits(&v->gb, 2)];
    if (v->dquant)
    {
        vop_dquant_decoding(v);
    }

    if (v->vstransform)
    {
        v->ttmbf = get_bits(&v->gb, 1);
        if (v->ttmbf)
        {
            v->ttfrm = get_bits(&v->gb, 2);
            av_log(v, AV_LOG_INFO, "Transform used: %ix%i\n",
                   (v->ttfrm & 2) ? 4 : 8, (v->ttfrm & 1) ? 4 : 8);
        }
    }
    /* Epilog should be done in caller */
    return 0;
}

static int decode_i_picture_header(VC9Context *v)
{
  int pqindex, status = 0, ac_pred, condover;

    /* Prolog common to all frametypes should be done in caller */
    //BF = Buffer Fullness
    if (v->profile <= PROFILE_MAIN && get_bits(&v->gb, 7))
    {
        av_log(v, AV_LOG_DEBUG, "I BufferFullness not 0\n");
    }

    /* Quantizer stuff */
    pqindex = get_bits(&v->gb, 5);
    if (v->quantizer_mode == QUANT_FRAME_IMPLICIT)
        v->pq = pquant_table[0][pqindex];
    else
    {
        v->pq = pquant_table[v->quantizer_mode-1][pqindex];
    }
    if (pqindex < 9) v->halfpq = get_bits(&v->gb, 1);
    if (v->quantizer_mode == QUANT_FRAME_EXPLICIT)
        v->pquantizer = get_bits(&v->gb, 1);
    av_log(v->avctx, AV_LOG_DEBUG, "I frame: QP=%i (+%i/2)\n",
           v->pq, v->halfpq);
#if HAS_ADVANCED_PROFILE
    if (v->profile <= PROFILE_MAIN)
#endif
    {
        if (v->extended_mv) v->mvrange = get_prefix(&v->gb, 0, 3);
        if (v->multires) v->respic = get_bits(&v->gb, 2);
    }
#if HAS_ADVANCED_PROFILE
    else
    {
        ac_pred = get_bits(&v->gb, 1);
        if (v->postprocflag) v->postproc = get_bits(&v->gb, 1);
        /* 7.1.1.34 + 8.5.2 */
        if (v->overlap && v->pq<9)
        {
            condover = get_bits(&v->gb, 1);
            if (condover)
            {
                condover = 2+get_bits(&v->gb, 1);
                if (condover == 3)
                    status = bitplane_decoding(v->over_flags_plane,
                                                   v->width_mb, v->height_mb, v);
            }
        }
    }
#endif

    /* Epilog should be done in caller */
    return status;
}

static int decode_p_picture_header(VC9Context *v)
{
    /* INTERFRM, FRMCNT, RANGEREDFRM read in caller */
    int lowquant, pqindex;

    pqindex = get_bits(&v->gb, 5);
    if (v->quantizer_mode == QUANT_FRAME_IMPLICIT)
        v->pq = pquant_table[0][pqindex];
    else
    {
        v->pq = pquant_table[v->quantizer_mode-1][pqindex];
    }
    if (pqindex < 9) v->halfpq = get_bits(&v->gb, 1);
    if (v->quantizer_mode == QUANT_FRAME_EXPLICIT)
        v->pquantizer = get_bits(&v->gb, 1);
    av_log(v->avctx, AV_LOG_DEBUG, "P Frame: QP=%i (+%i/2)\n",
           v->pq, v->halfpq);
    if (v->extended_mv == 1) v->mvrange = get_prefix(&v->gb, 0, 3);
#if HAS_ADVANCED_PROFILE
    if (v->profile > PROFILE_MAIN)
    {
        if (v->postprocflag) v->postproc = get_bits(&v->gb, 1);
    }
    else
#endif
        if (v->multires) v->respic = get_bits(&v->gb, 2);
    lowquant = (v->pquantizer>12) ? 0 : 1;
    v->mv_mode = mv_pmode_table[lowquant][get_prefix(&v->gb, 1, 4)];
    if (v->mv_mode == MV_PMODE_INTENSITY_COMP)
    {
        v->mv_mode2 = mv_pmode_table[lowquant][get_prefix(&v->gb, 1, 3)];
        v->lumscale = get_bits(&v->gb, 6);
        v->lumshift = get_bits(&v->gb, 6);
    }

    if ((v->mv_mode == MV_PMODE_INTENSITY_COMP &&
         v->mv_mode2 == MV_PMODE_MIXED_MV)
        || v->mv_mode == MV_PMODE_MIXED_MV)
    {
        if (bitplane_decoding(v->mv_type_mb_plane, v->width_mb,
                                  v->height_mb, v) < 0)
            return -1;
    }

    if (bitplane_decoding(v->skip_mb_plane, v->width_mb,
                              v->height_mb, v) < 0)
        return -1;

    /* Hopefully this is correct for P frames */
    v->mv_diff_vlc = &vc9_mv_diff_vlc[get_bits(&v->gb, 2)];
    v->cbpcy_vlc = &vc9_cbpcy_p_vlc[get_bits(&v->gb, 2)];

    if (v->dquant)
    {
        av_log(v->avctx, AV_LOG_INFO, "VOP DQuant info\n");
        vop_dquant_decoding(v);
    }

    if (v->vstransform)
    {
        v->ttmbf = get_bits(&v->gb, 1);
        if (v->ttmbf)
        {
            v->ttfrm = get_bits(&v->gb, 2);
            av_log(v->avctx, AV_LOG_INFO, "Transform used: %ix%i\n",
                   (v->ttfrm & 2) ? 4 : 8, (v->ttfrm & 1) ? 4 : 8);
        }
    }
    /* Epilog should be done in caller */
    return 0;
}


static int standard_decode_picture_header(VC9Context *v)
{
    int status = 0;

    if (v->finterpflag) v->interpfrm = get_bits(&v->gb, 1);
    skip_bits(&v->gb, 2); //framecnt unused
    if (v->rangered) v->rangeredfrm = get_bits(&v->gb, 1);
    v->pict_type = get_bits(&v->gb, 1);
    if (v->avctx->max_b_frames && !v->pict_type)
    {
        if (get_bits(&v->gb, 1)) v->pict_type = I_TYPE;
        else v->pict_type = P_TYPE;
    }
    else v->pict_type++; //P_TYPE

    switch (v->pict_type)
    {
    case I_TYPE: status = decode_i_picture_header(v); break;
    case BI_TYPE: status = decode_b_picture_header(v); break;
    case P_TYPE: status = decode_p_picture_header(v); break;
    case B_TYPE: status = decode_b_picture_header(v); break;
    }

    if (status == FRAME_SKIPED)
    {
      av_log(v, AV_LOG_INFO, "Skipping frame...\n");
      return status;
    }

    /* AC/DC Syntax */
    v->transacfrm = get_bits(&v->gb, 1);
    if (v->transacfrm) v->transacfrm += get_bits(&v->gb, 1);
    if (v->pict_type == I_TYPE || v->pict_type == BI_TYPE)
    {
        v->transacfrm2 = get_bits(&v->gb, 1);
        if (v->transacfrm2) v->transacfrm2 += get_bits(&v->gb, 1);
    }
    v->transacdctab = get_bits(&v->gb, 1);
   
    return 0;
}


#if HAS_ADVANCED_PROFILE
/******************************************************************************/
/* Advanced Profile picture header decoding specific functions                */
/******************************************************************************/
static int advanced_decode_picture_header(VC9Context *v)
{
    static const int type_table[4] = { P_TYPE, B_TYPE, I_TYPE, BI_TYPE };
    int type, i, ret;

    if (v->interlace)
    {
        v->fcm = get_bits(&v->gb, 1);
        if (v->fcm) v->fcm = 2+get_bits(&v->gb, 1);
    }

    type = get_prefix(&v->gb, 0, 4);
    if (type > 4 || type < 0) return FRAME_SKIPED;
    v->pict_type = type_table[type];
    av_log(v->avctx, AV_LOG_INFO, "AP Frame Type: %i\n", v->pict_type);

    if (v->tfcntrflag) v->tfcntr = get_bits(&v->gb, 8);
    if (v->broadcast)
    {
        if (!v->interlace) v->rptfrm = get_bits(&v->gb, 2);
        else
        {
            v->tff = get_bits(&v->gb, 1);
            v->rff = get_bits(&v->gb, 1);
        }
    }

    if (v->panscanflag)
    {
#if 0
        for (i=0; i<v->numpanscanwin; i++)
        {
            v->topleftx[i] = get_bits(&v->gb, 16);
            v->toplefty[i] = get_bits(&v->gb, 16);
            v->bottomrightx[i] = get_bits(&v->gb, 16);
            v->bottomrighty[i] = get_bits(&v->gb, 16);
        }
#else
        skip_bits(&v->gb, 16*4*v->numpanscanwin);
#endif
    }
    v->rndctrl = get_bits(&v->gb, 1);
    v->uvsamp = get_bits(&v->gb, 1);
    if (v->finterpflag == 1) v->interpfrm = get_bits(&v->gb, 1);

    switch(v->pict_type)
    {
    case I_TYPE: if (decode_i_picture_header(v) < 0) return -1;
    case P_TYPE: if (decode_p_picture_header(v) < 0) return -1;
    case BI_TYPE:
    case B_TYPE: if (decode_b_picture_header(v) < 0) return FRAME_SKIPED;
    default: break;
    }

    /* AC/DC Syntax */
    v->transacfrm = get_bits(&v->gb, 1);
    if (v->transacfrm) v->transacfrm += get_bits(&v->gb, 1);
    if (v->pict_type == I_TYPE || v->pict_type == BI_TYPE)
    {
        v->transacfrm2 = get_bits(&v->gb, 1);
        if (v->transacfrm2) v->transacfrm2 += get_bits(&v->gb, 1);
    }
    v->transacdctab = get_bits(&v->gb, 1);
    if (v->pict_type == I_TYPE) vop_dquant_decoding(v);

    return 0;
}
#endif

/******************************************************************************/
/* MacroBlock decoding functions                                              */
/******************************************************************************/
static int standard_decode_i_mbs(VC9Context *v)
{
    int x, y, ac_pred, cbpcy;

    /* Select ttmb table depending on pq */
    if (v->pq < 5) v->ttmb_vlc = &vc9_ttmb_vlc[0];
    else if (v->pq < 13) v->ttmb_vlc = &vc9_ttmb_vlc[1];
    else v->ttmb_vlc = &vc9_ttmb_vlc[2];

    for (y=0; y<v->height_mb; y++)
    {
        for (x=0; x<v->width_mb; x++)
        {
            cbpcy = get_vlc2(&v->gb, vc9_cbpcy_i_vlc.table,
                             VC9_CBPCY_I_VLC_BITS, 2);
            ac_pred = get_bits(&v->gb, 1);
            //Decode blocks from that mb wrt cbpcy
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
        mquant = (get_bits(&v->gb, 1)) ? v->pq : v->altpq;     \
      }                                                        \
      else                                                     \
      {                                                        \
        mqdiff = get_bits(&v->gb, 3);                          \
        if (mqdiff != 7) mquant = v->pq + mqdiff;              \
        else mquant = get_bits(&v->gb, 5);                     \
      }                                                        \
    }                                                          \
  }

/* MVDATA decoding from 8.3.5.2 */
#define GET_MVDATA()                                           \
  index = 1 + get_vlc2(&v->gb, v->mv_diff_vlc->table,          \
                       VC9_MV_DIFF_VLC_BITS, 2);               \
  if (index > 36)                                              \
  {                                                            \
    mb_has_coeffs = 1;                                         \
    index -= 37;                                               \
  }                                                            \
  else mb_has_coeffs = 0;                                      \
  mb_is_intra = 0;                                             \
  if (!index) { dmv_x = dmv_y = 0; }                           \
  else if (index == 35)                                        \
  {                                                            \
    dmv_x = get_bits(&v->gb, k_x);                             \
    dmv_y = get_bits(&v->gb, k_y);                             \
    mb_is_intra = 1;                                           \
  }                                                            \
  else                                                         \
  {                                                            \
    index1 = index%6;                                          \
    if (hpel_flag && index1 == 5) val = 1;                     \
    else val = 0;                                              \
    val = get_bits(&v->gb, size_table[index1] - val);          \
    sign = 0 - (val&1);                                        \
    dmv_x = (sign ^ ((val>>1) + offset_table[index1])) - sign; \
                                                               \
    index1 = index/6;                                          \
    if (hpel_flag && index1 == 5) val = 1;                     \
    else val = 0;                                              \
    val = get_bits(&v->gb, size_table[index1] - val);          \
    sign = 0 - (val&1);                                        \
    dmv_y = (sign ^ ((val>>1) + offset_table[index1])) - sign; \
  }

static int decode_p_mbs(VC9Context *v)
{
    int x, y, current_mb = 0, i; /* MB/Block Position info */
    int skip_mb_bit = 0, cbpcy; /* MB/B skip */
    int hybrid_pred, ac_pred; /* Prediction types */
    int mb_has_coeffs = 1  /* last_flag */, mb_is_intra;
    int dmv_x, dmv_y; /* Differential MV components */
    int mv_mode_bit = 0; /* mv_mode_bit: 1MV=0, 4MV=0 */
    int mqdiff, mquant; /* MB quantization */
    int tt_block; /* MB Transform type */
    static const int size_table[6] = { 0, 2, 3, 4, 5, 8 },
        offset_table[6] = { 0, 1, 3, 7, 15, 31 };
    int k_x, k_y; /* Long MV fixed bitlength */
    int hpel_flag, intra_flag; /* Some MB properties */
    int index, index1; /* LUT indices */
    int val, sign;

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

    for (y=0; y<v->height_mb; y++)
    {
        for (x=0; x<v->width_mb; x++)
        {
            if (v->mv_type_mb_plane[current_mb])
                mv_mode_bit = get_bits(&v->gb, 1);
            if (0) //skipmb is rawmode
                skip_mb_bit = get_bits(&v->gb, 1);
            if (!mv_mode_bit) /* 1MV mode */
            {
                if (!v->skip_mb_plane[current_mb])
                {
                    GET_MVDATA();

                    /* hybrid mv pred, 8.3.5.3.4 */
                    if (v->mv_mode == MV_PMODE_1MV ||
                        v->mv_mode == MV_PMODE_MIXED_MV)
                        hybrid_pred = get_bits(&v->gb, 1);
                    if (mb_is_intra && !mb_has_coeffs)
                    {
                        GET_MQUANT();
                        ac_pred = get_bits(&v->gb, 1);
                    }
                    else if (mb_has_coeffs)
                    {
                        if (mb_is_intra) ac_pred = get_bits(&v->gb, 1);
                        cbpcy = get_vlc2(&v->gb, v->cbpcy_vlc->table,
                                         VC9_CBPCY_P_VLC_BITS, 2);
                        GET_MQUANT();
                    }
                    if (!v->ttmbf)
                        v->ttfrm = get_vlc2(&v->gb, v->ttmb_vlc->table,
                                            VC9_TTMB_VLC_BITS, 12);
                    //Decode blocks from that mb wrt cbpcy
                }
                else //Skipped
                {
                    /* hybrid mv pred, 8.3.5.3.4 */
                    if (v->mv_mode == MV_PMODE_1MV ||
                        v->mv_mode == MV_PMODE_MIXED_MV)
                        hybrid_pred = get_bits(&v->gb, 1);
                }
            } //1MV mode
            else //4MV mode
            {
              if (!v->skip_mb_plane[current_mb] /* unskipped MB */)
                {
                    cbpcy = get_vlc2(&v->gb, v->cbpcy_vlc->table,
                                     VC9_CBPCY_P_VLC_BITS, 2);
                    for (i=0; i<4; i++) //For all 4 Y blocks
                    {
                        if (cbpcy & (1<<6) /* cbpcy set for this block */)
                        {
                            GET_MVDATA();
                        }
                        if (v->mv_mode == MV_PMODE_MIXED_MV /* Hybrid pred */)
                            hybrid_pred = get_bits(&v->gb, 1);
                        GET_MQUANT();
                        if (mb_is_intra /* One of the 4 blocks is intra */ &&
                            index /* non-zero pred for that block */)
                            ac_pred = get_bits(&v->gb, 1);
                        if (!v->ttmbf)
                            tt_block = get_vlc2(&v->gb, v->ttmb_vlc->table,
                                                VC9_TTMB_VLC_BITS, 12);
            
                        /* TODO: Process blocks wrt cbpcy */
            
                        /* Prepare cbpcy for next block */
                        cbpcy <<= 1;
                    }
                }
                else //Skipped MB
                {
                    for (i=0; i<4; i++) //All 4 Y blocks
                    {
                        if (v->mv_mode == MV_PMODE_MIXED_MV /* Hybrid pred */)
                            hybrid_pred = get_bits(&v->gb, 1);
                        
                        /* FIXME: do something */
                    }
                }
            }
        }
        current_mb++;
    }
    return 0;
}

static int decode_b_mbs(VC9Context *v)
{
    int x, y, current_mb = 0 , last_mb = v->height_mb*v->width_mb,
        i /* MB / B postion information */;
    int direct_b_bit = 0, skip_mb_bit = 0;
    int ac_pred;
    int b_mv1 = 0, b_mv2 = 0, b_mv_type = 0;
    int mquant, mqdiff; /* MB quant stuff */
    int tt_block; /* Block transform type */
    
    for (y=0; y<v->height_mb; y++)
    {
        for (x=0; x<v->width_mb; x++)
        {
            if (v->direct_mb_plane[current_mb])
              direct_b_bit = get_bits(&v->gb, 1);
            if (1 /* Skip mode is raw */)
            {
                /* FIXME getting tired commenting */
#if 0
                skip_mb_bit = get_bits(&v->gb, n); //vlc
#endif
            }
            if (!direct_b_bit)
            {
                if (skip_mb_bit)
                {
                    /* FIXME getting tired commenting */
#if 0
                    b_mv_type = get_bits(&v->gb, n); //vlc
#endif
                }
                else
                { 
                    /* FIXME getting tired commenting */
#if 0
                    b_mv1 = get_bits(&v->gb, n); //VLC
#endif
                    if (1 /* b_mv1 isn't intra */)
                    {
                        /* FIXME: actually read it */
                        b_mv_type = 0; //vlc
                    }
                }
            }
            if (!skip_mb_bit)
            {
                if (b_mv1 != last_mb)
                {
                    GET_MQUANT();
                    if (1 /* intra mb */)
                        ac_pred = get_bits(&v->gb, 1);
                }
                else
                {
                    if (1 /* forward_mb is interpolate */)
                    {
                        /* FIXME: actually read it */
                        b_mv2 = 0; //vlc
                    }
                    if (1 /* b_mv2 isn't the last */)
                    {
                        if (1 /* intra_mb */)
                            ac_pred = get_bits(&v->gb, 1);
                        GET_MQUANT();
                    }
                }
            }
            //End1
            /* FIXME getting tired, commenting */
#if 0
            if (v->ttmbf)
                v->ttmb = get_bits(&v->gb, n); //vlc
#endif
        }
        //End2
        for (i=0; i<6; i++)
        {
            /* FIXME: process the block */
        }

        current_mb++;
    }
    return 0;
}

#if HAS_ADVANCED_PROFILE
static int advanced_decode_i_mbs(VC9Context *v)
{
  int i, x, y, cbpcy, mqdiff, absmq, mquant, ac_pred, condover,
    current_mb = 0, over_flags_mb = 0;

    for (y=0; y<v->height_mb; y++)
    {
        for (x=0; x<v->width_mb; x++)
        {
            if (v->ac_pred_plane[i])
                ac_pred = get_bits(&v->gb, 1);
            if (condover == 3 && v->over_flags_plane)
                over_flags_mb = get_bits(&v->gb, 1);
            GET_MQUANT();
        }
        current_mb++;
    }
    return 0;
}
#endif

static int vc9_decode_init(AVCodecContext *avctx)
{
    VC9Context *v = avctx->priv_data;
    GetBitContext gb;

    if (!avctx->extradata_size || !avctx->extradata) return -1;
    avctx->pix_fmt = PIX_FMT_YUV420P;
    v->avctx = avctx;

    if (init_common(v) < 0) return -1;

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

    /* Done with header parsing */
    //FIXME I feel like this is wrong
    v->width_mb = (avctx->coded_width+15)>>4;
    v->height_mb = (avctx->coded_height+15)>>4;

    /* Allocate mb bitplanes */
    v->mv_type_mb_plane = (uint8_t *)av_malloc(v->width_mb*v->height_mb);
    if (!v->mv_type_mb_plane) return -1;
    v->skip_mb_plane = (uint8_t *)av_malloc(v->width_mb*v->height_mb);
    if (!v->skip_mb_plane) return -1;
    v->direct_mb_plane = (uint8_t *)av_malloc(v->width_mb*v->height_mb);
    if (!v->direct_mb_plane) return -1;

#if HAS_ADVANCED_PROFILE
    if (v->profile > PROFILE_MAIN)
    {
        v->over_flags_plane = (uint8_t *)av_malloc(v->width_mb*v->height_mb);
        if (!v->over_flags_plane) return -1;
        v->ac_pred_plane = (uint8_t *)av_malloc(v->width_mb*v->height_mb);
        if (!v->ac_pred_plane) return -1;
    }
#endif

    return 0;
}

static int vc9_decode_frame(AVCodecContext *avctx,
                            void *data, int *data_size,
                            uint8_t *buf, int buf_size)
{
    VC9Context *v = avctx->priv_data;
    int ret = FRAME_SKIPED, len, start_code;
    AVFrame *pict = data;
    uint8_t *tmp_buf;
    v->avctx = avctx;

    //buf_size = 0 -> last frame
    if (!buf_size) return 0;

    len = avpicture_get_size(avctx->pix_fmt, avctx->width,
                             avctx->height);
    tmp_buf = (uint8_t *)av_mallocz(len);
    avpicture_fill((AVPicture *)pict, tmp_buf, avctx->pix_fmt,
                   avctx->width, avctx->height);

    if (avctx->codec_id == CODEC_ID_WMV3)
    {
	init_get_bits(&v->gb, buf, buf_size*8);
	av_log(avctx, AV_LOG_INFO, "Frame: %i bits to decode\n", buf_size*8);
        
#if HAS_ADVANCED_PROFILE
	if (v->profile > PROFILE_MAIN)
	{
    	    if (advanced_decode_picture_header(v) == FRAME_SKIPED) return buf_size;
    	    switch(v->pict_type)
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
    	    if (standard_decode_picture_header(v) == FRAME_SKIPED) return buf_size;
    	    switch(v->pict_type)
	    {
    		case I_TYPE: ret = standard_decode_i_mbs(v); break;
    		case P_TYPE: ret = decode_p_mbs(v); break;
    		case B_TYPE:
    		case BI_TYPE: ret = decode_b_mbs(v); break;
    		default: ret = FRAME_SKIPED;
    	    }
    	    if (ret == FRAME_SKIPED) return buf_size;
	}

	/* Size of the output data = image */
	av_log(avctx, AV_LOG_DEBUG, "Consumed %i/%i bits\n",
           get_bits_count(&v->gb), buf_size*8);
    }
    else
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

	    init_get_bits(&v->gb, buf+i, (buf_size-i)*8);
	
	    switch(scs)
	    {
		case 0xf:
		    decode_sequence_header(avctx, &v->gb);
		    break;
		// to be finished
	    }
	    
	    i += get_bits_count(&v->gb)*8;
	}
#else
	av_abort();
#endif
    }

    *data_size = len;

    /* Fake consumption of all data */
    return buf_size; //Number of bytes consumed
}

static int vc9_decode_end(AVCodecContext *avctx)
{
    VC9Context *v = avctx->priv_data;

#if HAS_ADVANCED_PROFILE
    if (v->hrd_rate) av_free(v->hrd_rate);
    if (v->hrd_buffer) av_free(v->hrd_buffer);
#endif
    if (v->mv_type_mb_plane) av_free(v->mv_type_mb_plane);
    if (v->skip_mb_plane) av_free(v->skip_mb_plane);
    if (v->direct_mb_plane) av_free(v->direct_mb_plane);
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
