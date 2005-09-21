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
 * TODO: most AP stuff, optimize, most of MB layer, transform, filtering and motion compensation, etc
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
#define HAS_ADVANCED_PROFILE   0
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

/** Available Profiles */
//@{
#define PROFILE_SIMPLE   0
#define PROFILE_MAIN     1
#define PROFILE_COMPLEX  2 ///< TODO: WMV9 specific
#define PROFILE_ADVANCED 3
//@}

/** Sequence quantizer mode */
//@{
#define QUANT_FRAME_IMPLICIT   0 ///< Implicitly specified at frame level
#define QUANT_FRAME_EXPLICIT   1 ///< Explicitly specified at frame level
#define QUANT_NON_UNIFORM      2 ///< Non-uniform quant used for all frames
#define QUANT_UNIFORM          3 ///< Uniform quant used for all frames
//@}

/** Where quant can be changed */
//@{
#define DQPROFILE_FOUR_EDGES   0
#define DQPROFILE_DOUBLE_EDGES 1
#define DQPROFILE_SINGLE_EDGE  2
#define DQPROFILE_ALL_MBS      3
//@}

/** @name Where quant can be changed
 */
//@{
#define DQPROFILE_FOUR_EDGES   0
#define DQSINGLE_BEDGE_LEFT   0
#define DQSINGLE_BEDGE_TOP    1
#define DQSINGLE_BEDGE_RIGHT  2
#define DQSINGLE_BEDGE_BOTTOM 3
//@}

/** Which pair of edges is quantized with ALTPQUANT */
//@{
#define DQDOUBLE_BEDGE_TOPLEFT     0
#define DQDOUBLE_BEDGE_TOPRIGHT    1
#define DQDOUBLE_BEDGE_BOTTOMRIGHT 2
#define DQDOUBLE_BEDGE_BOTTOMLEFT  3
//@}

/** MV modes for P frames */
//@{
#define MV_PMODE_1MV_HPEL_BILIN   0
#define MV_PMODE_1MV              1
#define MV_PMODE_1MV_HPEL         2
#define MV_PMODE_MIXED_MV         3
#define MV_PMODE_INTENSITY_COMP   4
//@}

/** @name MV types for B frames */
//@{
#define BMV_TYPE_BACKWARD          0
#define BMV_TYPE_BACKWARD          0
#define BMV_TYPE_FORWARD           1
#define BMV_TYPE_INTERPOLATED      3
//@}

/** MV P mode - the 5th element is only used for mode 1 */
static const uint8_t mv_pmode_table[2][5] = {
  { MV_PMODE_1MV_HPEL_BILIN, MV_PMODE_1MV, MV_PMODE_1MV_HPEL, MV_PMODE_MIXED_MV, MV_PMODE_INTENSITY_COMP },
  { MV_PMODE_1MV, MV_PMODE_MIXED_MV, MV_PMODE_1MV_HPEL, MV_PMODE_1MV_HPEL_BILIN, MV_PMODE_INTENSITY_COMP }
};

/** One more frame type */
#define BI_TYPE 7

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

/** @name VC-9 VLC tables and defines
 *  @todo TODO move this into the context
 */
//@{
#define VC9_BFRACTION_VLC_BITS 7
static VLC vc9_bfraction_vlc;
#define VC9_IMODE_VLC_BITS 4
static VLC vc9_imode_vlc;
#define VC9_NORM2_VLC_BITS 3
static VLC vc9_norm2_vlc;
#define VC9_NORM6_VLC_BITS 9
static VLC vc9_norm6_vlc;
/* Could be optimized, one table only needs 8 bits */
#define VC9_TTMB_VLC_BITS 9 //12
static VLC vc9_ttmb_vlc[3];
#define VC9_MV_DIFF_VLC_BITS 9 //15
static VLC vc9_mv_diff_vlc[4];
#define VC9_CBPCY_P_VLC_BITS 9 //14
static VLC vc9_cbpcy_p_vlc[4];
#define VC9_4MV_BLOCK_PATTERN_VLC_BITS 6
static VLC vc9_4mv_block_pattern_vlc[4];
#define VC9_TTBLK_VLC_BITS 5
static VLC vc9_ttblk_vlc[3];
#define VC9_SUBBLKPAT_VLC_BITS 6
static VLC vc9_subblkpat_vlc[3];
//@}

/** Bitplane struct
 * We mainly need data and is_raw, so this struct could be avoided
 * to save a level of indirection; feel free to modify
 * @fixme For now, stride=width
 * @warning Data are bits, either 1 or 0
 */
typedef struct BitPlane {
    uint8_t *data;      ///< Data buffer
    int width;          ///< Width of the buffer
    int stride;         ///< Stride of the buffer
    int height;         ///< Plane height
    uint8_t is_raw;     ///< Bit values must be read at MB level
} BitPlane;

/** The VC9 Context
 * @fixme Change size wherever another size is more efficient
 * Many members are only used for Advanced Profile
 */
typedef struct VC9Context{
    MpegEncContext s;

    /** Simple/Main Profile sequence header */
    //@{
    int res_sm;           ///< reserved, 2b
    int res_x8;           ///< reserved
    int multires;         ///< frame-level RESPIC syntax element present
    int res_fasttx;       ///< reserved, always 1
    int res_transtab;     ///< reserved, always 0
    int rangered;         ///< RANGEREDFRM (range reduction) syntax element present
                          ///< at frame level
    int res_rtm_flag;     ///< reserved, set to 1
    int reserved;         ///< reserved
    //@}

#if HAS_ADVANCED_PROFILE
    /** Advanced Profile */
    //@{
    int level;            ///< 3bits, for Advanced/Simple Profile, provided by TS layer
    int chromaformat;     ///< 2bits, 2=4:2:0, only defined
    int postprocflag;     ///< Per-frame processing suggestion flag present
    int broadcast;        ///< TFF/RFF present
    int interlace;        ///< Progressive/interlaced (RPTFTM syntax element)
    int tfcntrflag;       ///< TFCNTR present
    int panscanflag;      ///< NUMPANSCANWIN, TOPLEFT{X,Y}, BOTRIGHT{X,Y} present
    int extended_dmv;     ///< Additional extended dmv range at P/B frame-level
    int color_prim;       ///< 8bits, chroma coordinates of the color primaries
    int transfer_char;    ///< 8bits, Opto-electronic transfer characteristics
    int matrix_coef;      ///< 8bits, Color primaries->YCbCr transform matrix
    int hrd_param_flag;   ///< Presence of Hypothetical Reference
                          ///< Decoder parameters
    //@}
#endif


    /** Sequence header data for all Profiles
     * TODO: choose between ints, uint8_ts and monobit flags
     */
    //@{
    int profile;          ///< 2bits, Profile
    int frmrtq_postproc;  ///< 3bits, 
    int bitrtq_postproc;  ///< 5bits, quantized framerate-based postprocessing strength
    int fastuvmc;         ///< Rounding of qpel vector to hpel ? (not in Simple)
    int extended_mv;      ///< Ext MV in P/B (not in Simple)
    int dquant;           ///< How qscale varies with MBs, 2bits (not in Simple)
    int vstransform;      ///< variable-size [48]x[48] transform type + info
    int overlap;          ///< overlapped transforms in use
    int quantizer_mode;   ///< 2bits, quantizer mode used for sequence, see QUANT_*
    int finterpflag;      ///< INTERPFRM present
    //@}

    /** Frame decoding info for all profiles */
    //@{
    uint8_t mv_mode;      ///< MV coding monde
    uint8_t mv_mode2;     ///< Secondary MV coding mode (B frames)
    int k_x;              ///< Number of bits for MVs (depends on MV range)
    int k_y;              ///< Number of bits for MVs (depends on MV range)
    uint8_t pq, altpq;    ///< Current/alternate frame quantizer scale
    /** pquant parameters */
    //@{
    uint8_t dquantfrm;
    uint8_t dqprofile;
    uint8_t dqsbedge;
    uint8_t dqbilevel;
    //@}
    /** AC coding set indexes
     * @see 8.1.1.10, p(1)10
     */
    //@{
    int c_ac_table_index; ///< Chroma index from ACFRM element
    int y_ac_table_index; ///< Luma index from AC2FRM element
    //@}
    int ttfrm;            ///< Transform type info present at frame level
    uint8_t ttmbf;        ///< Transform type flag
    int ttmb;             ///< Transform type
    uint8_t ttblk4x4;     ///< Value of ttblk which indicates a 4x4 transform
    /** Luma compensation parameters */
    //@{
    uint8_t lumscale;
    uint8_t lumshift;
    //@}
    int16_t bfraction;    ///< Relative position % anchors=> how to scale MVs
    uint8_t halfpq;       ///< Uniform quant over image and qp+.5
    uint8_t respic;       ///< Frame-level flag for resized images
    int buffer_fullness;  ///< HRD info
    /** Ranges:
     * -# 0 -> [-64n 63.f] x [-32, 31.f]
     * -# 1 -> [-128, 127.f] x [-64, 63.f]
     * -# 2 -> [-512, 511.f] x [-128, 127.f]
     * -# 3 -> [-1024, 1023.f] x [-256, 255.f]
     */
    uint8_t mvrange;
    uint8_t pquantizer;           ///< Uniform (over sequence) quantizer in use
    uint8_t *previous_line_cbpcy; ///< To use for predicted CBPCY
    VLC *cbpcy_vlc;               ///< CBPCY VLC table
    int tt_index;                 ///< Index for Transform Type tables
    BitPlane mv_type_mb_plane;    ///< bitplane for mv_type == (4MV)
    BitPlane skip_mb_plane;       ///< bitplane for skipped MBs
    BitPlane direct_mb_plane;     ///< bitplane for "direct" MBs

    /** Frame decoding info for S/M profiles only */
    //@{
    uint8_t rangeredfrm; ///< out_sample = CLIP((in_sample-128)*2+128)
    uint8_t interpfrm;
    //@}

#if HAS_ADVANCED_PROFILE
    /** Frame decoding info for Advanced profile */
    //@{
    uint8_t fcm; ///< 0->Progressive, 2->Frame-Interlace, 3->Field-Interlace
    uint8_t numpanscanwin;
    uint8_t tfcntr;
    uint8_t rptfrm, tff, rff;
    uint16_t topleftx;
    uint16_t toplefty;
    uint16_t bottomrightx;
    uint16_t bottomrighty;
    uint8_t uvsamp;
    uint8_t postproc;
    int hrd_num_leaky_buckets;
    uint8_t bit_rate_exponent;
    uint8_t buffer_size_exponent;
    BitPlane ac_pred_plane;       ///< AC prediction flags bitplane
    BitPlane over_flags_plane;    ///< Overflags bitplane
    uint8_t condover;
    uint16_t *hrd_rate, *hrd_buffer;
    uint8_t *hrd_fullness;
    uint8_t range_mapy_flag;
    uint8_t range_mapuv_flag;
    uint8_t range_mapy;
    uint8_t range_mapuv;
    //@}
#endif
} VC9Context;

/**
 * Get unary code of limited length
 * @fixme FIXME Slow and ugly
 * @param gb GetBitContext
 * @param[in] stop The bitstop value (unary code of 1's or 0's)
 * @param[in] len Maximum length
 * @return Unary length/index
 */
static int get_prefix(GetBitContext *gb, int stop, int len)
{
#if 1
  int i = 0, tmp = !stop;

  while (i != len && tmp != stop)
  {
    tmp = get_bits(gb, 1);
    i++;
  }
  if (i == len && tmp != stop) return len+1;
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

/**
 * Init VC-9 specific tables and VC9Context members
 * @param v The VC9Context to initialize
 * @return Status
 */
static int vc9_init_common(VC9Context *v)
{
    static int done = 0;
    int i = 0;

    /* Set the bit planes */
    v->mv_type_mb_plane = (struct BitPlane) { NULL, 0, 0, 0 };
    v->direct_mb_plane = (struct BitPlane) { NULL, 0, 0, 0 };
    v->skip_mb_plane = (struct BitPlane) { NULL, 0, 0, 0 };
#if HAS_ADVANCED_PROFILE
    v->ac_pred_plane = v->over_flags_plane = (struct BitPlane) { NULL, 0, 0, 0 };
    v->hrd_rate = v->hrd_buffer = NULL;
#endif

    /* VLC tables */
#if 0 // spec -> actual tables converter
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
        INIT_VLC(&vc9_norm6_vlc, VC9_NORM6_VLC_BITS, 64,
                 vc9_norm6_bits, 1, 1,
                 vc9_norm6_codes, 2, 2, 1);
        INIT_VLC(&vc9_imode_vlc, VC9_IMODE_VLC_BITS, 7,
                 vc9_imode_bits, 1, 1,
                 vc9_imode_codes, 1, 1, 1);
        for (i=0; i<3; i++)
        {
            INIT_VLC(&vc9_ttmb_vlc[i], VC9_TTMB_VLC_BITS, 16,
                     vc9_ttmb_bits[i], 1, 1,
                     vc9_ttmb_codes[i], 2, 2, 1);
            INIT_VLC(&vc9_ttblk_vlc[i], VC9_TTBLK_VLC_BITS, 8,
                     vc9_ttblk_bits[i], 1, 1,
                     vc9_ttblk_codes[i], 1, 1, 1);
            INIT_VLC(&vc9_subblkpat_vlc[i], VC9_SUBBLKPAT_VLC_BITS, 15,
                     vc9_subblkpat_bits[i], 1, 1,
                     vc9_subblkpat_codes[i], 1, 1, 1);
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
/**
 * Decode sequence header's Hypothetic Reference Decoder data
 * @see 6.2.1, p32
 * @param v The VC9Context to initialize
 * @param gb A GetBitContext initialized from AVCodecContext extra_data
 * @return Status
 */
static int decode_hrd(VC9Context *v, GetBitContext *gb)
{
    int i, num;

    num = 1 + get_bits(gb, 5);

    /*hrd rate*/
    if (v->hrd_rate || num != v->hrd_num_leaky_buckets)
    {
        av_freep(&v->hrd_rate);
    }
    if (!v->hrd_rate) v->hrd_rate = av_malloc(num*sizeof(uint16_t));
    if (!v->hrd_rate) return -1;

    /*hrd buffer*/
    if (v->hrd_buffer || num != v->hrd_num_leaky_buckets)
    {
        av_freep(&v->hrd_buffer);
    }
    if (!v->hrd_buffer) v->hrd_buffer = av_malloc(num*sizeof(uint16_t));
    if (!v->hrd_buffer)
    {
        av_freep(&v->hrd_rate);
        return -1;
    }

    /*hrd fullness*/
    if (v->hrd_fullness || num != v->hrd_num_leaky_buckets)
    {
        av_freep(&v->hrd_buffer);
    }
    if (!v->hrd_fullness) v->hrd_fullness = av_malloc(num*sizeof(uint8_t));
    if (!v->hrd_fullness)
    {
        av_freep(&v->hrd_rate);
        av_freep(&v->hrd_buffer);
        return -1;
    }
    v->hrd_num_leaky_buckets = num;

    //exponent in base-2 for rate
    v->bit_rate_exponent = 6 + get_bits(gb, 4);
    //exponent in base-2 for buffer_size
    v->buffer_size_exponent = 4 + get_bits(gb, 4);

    for (i=0; i<num; i++)
    {
        //mantissae, ordered (if not, use a function ?
        v->hrd_rate[i] = 1 + get_bits(gb, 16);
        if (i && v->hrd_rate[i-1]>=v->hrd_rate[i])
        {
            av_log(v->s.avctx, AV_LOG_ERROR, "HDR Rates aren't strictly increasing:"
                   "%i vs %i\n", v->hrd_rate[i-1], v->hrd_rate[i]);
            return -1;
        }
        v->hrd_buffer[i] = 1 + get_bits(gb, 16);
        if (i && v->hrd_buffer[i-1]<v->hrd_buffer[i])
        {
            av_log(v->s.avctx, AV_LOG_ERROR, "HDR Buffers aren't decreasing:"
                   "%i vs %i\n", v->hrd_buffer[i-1], v->hrd_buffer[i]);
            return -1;
        }
    }
    return 0;
}

/**
 * Decode sequence header for Advanced Profile
 * @see Table 2, p18
 * @see 6.1.7, pp21-27
 * @param v The VC9Context to initialize
 * @param gb A GetBitContext initialized from AVCodecContext extra_data
 * @return Status
 */
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
        avctx->coded_width = get_bits(gb, 12) << 1;
        avctx->coded_height = get_bits(gb, 12) << 1;
        if ( get_bits(gb, 1) /* disp_size_flag */)
        {
            avctx->width = get_bits(gb, 14);
            avctx->height = get_bits(gb, 14);
        }

        /* 6.1.7.4, p23 */
        if ( get_bits(gb, 1) /* aspect_ratio_flag */)
        {
            aspect_ratio = get_bits(gb, 4); //SAR
            if (aspect_ratio == 0x0F) //FF_ASPECT_EXTENDED
            {
                avctx->sample_aspect_ratio.num = 1 + get_bits(gb, 8);
                avctx->sample_aspect_ratio.den = 1 + get_bits(gb, 8);
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
    if ( get_bits(gb, 1) /* framerateflag */)
    {
        if ( !get_bits(gb, 1) /* framerateind */)
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
                nr = 5; /* overflow protection */
            }
            if (dr<1)
            {
                av_log(avctx, AV_LOG_ERROR, "0 is forbidden for FRAMERATEDR\n");
                return -1;
            }
            if (dr>2)
            {
                av_log(avctx, AV_LOG_ERROR,
                       "Reserved FRAMERATEDR %i not handled\n", dr);
                dr = 2; /* overflow protection */
            }
            avctx->time_base.num = fps_nr[dr - 1];
            avctx->time_base.den = fps_nr[nr - 1];
        }
        else
        {
            nr = get_bits(gb, 16);
            // 0.03125->2048Hz / 0.03125Hz
            avctx->time_base.den = 1000000;
            avctx->time_base.num = 31250*(1+nr);
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
            av_log(avctx, AV_LOG_ERROR, "0 for COLOR_PRIM is forbidden\n");
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
        if (v->transfer_char < 1)
        {
            av_log(avctx, AV_LOG_ERROR, "0 for TRAMSFER_CHAR is forbidden\n");
            return -1;
        }
        if (v->transfer_char == 3 || v->transfer_char>8)
        {
            av_log(avctx, AV_LOG_DEBUG, "Reserved TRANSFERT_CHAR %i found\n",
                   v->color_prim);
            return -1;
        }

        //Matrix coefficient for primariev->YCbCr
        v->matrix_coef = get_bits(gb, 8);
        if (v->matrix_coef < 1)
        {
            av_log(avctx, AV_LOG_ERROR, "0 for MATRIX_COEF is forbidden\n");
            return -1;
        }
        if ((v->matrix_coef > 2 && v->matrix_coef < 6) || v->matrix_coef > 7)
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

    /*reset scaling ranges, 6.2.2 & 6.2.3, p33*/
    v->range_mapy_flag = 0;
    v->range_mapuv_flag = 0;

    av_log(avctx, AV_LOG_DEBUG, "Advanced profile not supported yet\n");
    return -1;
}
#endif

/** 
 * Decode Simple/Main Profiles sequence header
 * @see Figure 7-8, p16-17
 * @param avctx Codec context
 * @param gb GetBit context initialized from Codec context extra_data
 * @return Status
 */
static int decode_sequence_header(AVCodecContext *avctx, GetBitContext *gb)
{
    VC9Context *v = avctx->priv_data;

    av_log(avctx, AV_LOG_DEBUG, "Header: %0X\n", show_bits(gb, 32));
    v->profile = get_bits(gb, 2);
    if (v->profile == 2)
    {
        av_log(avctx, AV_LOG_ERROR, "Profile value 2 is forbidden\n");
        return -1;
    }

#if HAS_ADVANCED_PROFILE
    if (v->profile == PROFILE_ADVANCED)
    {
        v->level = get_bits(gb, 3);
        if(v->level >= 5)
        {
            av_log(avctx, AV_LOG_ERROR, "Reserved LEVEL %i\n",v->level);
        }
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
            return -1;
        }
    }

    // (fps-2)/4 (->30)
    v->frmrtq_postproc = get_bits(gb, 3); //common
    // (bitrate-32kbps)/64kbps
    v->bitrtq_postproc = get_bits(gb, 5); //common
    v->s.loop_filter = get_bits(gb, 1); //common
    if(v->s.loop_filter == 1 && v->profile == PROFILE_SIMPLE)
    {
        av_log(avctx, AV_LOG_ERROR,
               "LOOPFILTER shell not be enabled in simple profile\n");
    }

#if HAS_ADVANCED_PROFILE
    if (v->profile < PROFILE_ADVANCED)
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
    if (v->profile < PROFILE_ADVANCED)
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
    if (v->profile < PROFILE_ADVANCED)
#endif
    {
        v->s.resync_marker = get_bits(gb, 1);
        v->rangered = get_bits(gb, 1);
        if (v->rangered && v->profile == PROFILE_SIMPLE)
        {
            av_log(avctx, AV_LOG_DEBUG,
                   "RANGERED should be set to 0 in simple profile\n");
        }
    }

    v->s.max_b_frames = avctx->max_b_frames = get_bits(gb, 3); //common
    v->quantizer_mode = get_bits(gb, 2); //common

#if HAS_ADVANCED_PROFILE
    if (v->profile < PROFILE_ADVANCED)
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
               v->s.loop_filter, v->multires, v->fastuvmc, v->extended_mv,
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
/** Entry point decoding (Advanced Profile)
 * @param avctx Codec context
 * @param gb GetBit context initialized from avctx->extra_data
 * @return Status
 */
static int advanced_entry_point_process(AVCodecContext *avctx, GetBitContext *gb)
{
    VC9Context *v = avctx->priv_data;
    int i;
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
        assert(v->hrd_num_leaky_buckets > 0);
        for (i=0; i<v->hrd_num_leaky_buckets; i++)
            v->hrd_fullness[i] = get_bits(gb, 8);
    }
    if ((v->range_mapy_flag = get_bits(gb, 1)))
    {
        //RANGE_MAPY
        av_log(avctx, AV_LOG_DEBUG, "RANGE_MAPY\n");
        v->range_mapy = get_bits(gb, 3);
    }
    if ((v->range_mapuv_flag = get_bits(gb, 1)))
    {
        //RANGE_MAPUV
        av_log(avctx, AV_LOG_DEBUG, "RANGE_MAPUV\n");
        v->range_mapuv = get_bits(gb, 3);
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

/***********************************************************************/
/**
 * @defgroup bitplane VC9 Bitplane decoding
 * @see 8.7, p56
 * @{
 */

/** @addtogroup bitplane
 * Imode types
 * @{
 */
#define IMODE_RAW     0
#define IMODE_NORM2   1
#define IMODE_DIFF2   2
#define IMODE_NORM6   3
#define IMODE_DIFF6   4
#define IMODE_ROWSKIP 5
#define IMODE_COLSKIP 6
/** @} */ //imode defines

/** Allocate the buffer from a bitplane, given its dimensions
 * @param bp Bitplane which buffer is to allocate
 * @param[in] width Width of the buffer
 * @param[in] height Height of the buffer
 * @return Status
 * @todo TODO: Take into account stride
 * @todo TODO: Allow use of external buffers ?
 */
int alloc_bitplane(BitPlane *bp, int width, int height)
{
    if (!bp || bp->width<0 || bp->height<0) return -1;
    bp->data = (uint8_t*)av_malloc(width*height);
    if (!bp->data) return -1;
    bp->width = bp->stride = width; 
    bp->height = height;
    return 0;
}

/** Free the bitplane's buffer
 * @param bp Bitplane which buffer is to free
 */
void free_bitplane(BitPlane *bp)
{
    bp->width = bp->stride = bp->height = 0;
    if (bp->data) av_freep(&bp->data);
}

/** Decode rows by checking if they are skipped
 * @param plane Buffer to store decoded bits
 * @param[in] width Width of this buffer
 * @param[in] height Height of this buffer
 * @param[in] stride of this buffer
 */
static void decode_rowskip(uint8_t* plane, int width, int height, int stride, GetBitContext *gb){
    int x, y;

    for (y=0; y<height; y++){
        if (!get_bits(gb, 1)) //rowskip
            memset(plane, 0, width);
        else
            for (x=0; x<width; x++) 
                plane[x] = get_bits(gb, 1);
        plane += stride;
    }
}

/** Decode columns by checking if they are skipped
 * @param plane Buffer to store decoded bits
 * @param[in] width Width of this buffer
 * @param[in] height Height of this buffer
 * @param[in] stride of this buffer
 * @fixme FIXME: Optimize
 */
static void decode_colskip(uint8_t* plane, int width, int height, int stride, GetBitContext *gb){
    int x, y;

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

/** Decode a bitplane's bits
 * @param bp Bitplane where to store the decode bits
 * @param v VC9 context for bit reading and logging
 * @return Status
 * @fixme FIXME: Optimize
 * @todo TODO: Decide if a struct is needed
 */
static int bitplane_decoding(BitPlane *bp, VC9Context *v)
{
    GetBitContext *gb = &v->s.gb;

    int imode, x, y, code, use_vertical_tile, tile_w, tile_h, offset;
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
        if ((bp->height*bp->width) & 1)
        {
            *(++planep) = get_bits(gb, 1);
            offset = x = 1;
        }
        else offset = x = 0;

        for (y=0; y<bp->height; y++)
        {
            for(; x<bp->width; x+=2)
            {
                code = get_vlc2(gb, vc9_norm2_vlc.table, VC9_NORM2_VLC_BITS, 2);
                *(++planep) = code&1; //lsb => left
                *(++planep) = (code>>1)&1; //msb => right
            }
            planep += bp->stride-bp->width;
            if ((bp->width-offset)&1) //Odd number previously processed
            {
                code = get_vlc2(gb, vc9_norm2_vlc.table, VC9_NORM2_VLC_BITS, 2);
                *planep = code&1;
                planep += bp->stride-bp->width;
                *planep = (code>>1)&1; //msb => right
                offset = x = 1;
            }
            else
            {
                offset = x = 0;
                planep += bp->stride-bp->width;
            }
        }
        break;
    case IMODE_DIFF6:
    case IMODE_NORM6:
        use_vertical_tile=  bp->height%3==0 &&  bp->width%3!=0;
        tile_w= use_vertical_tile ? 2 : 3;
        tile_h= use_vertical_tile ? 3 : 2;

        for(y=  bp->height%tile_h; y< bp->height; y+=tile_h){
            for(x=  bp->width%tile_w; x< bp->width; x+=tile_w){
                code = get_vlc2(gb, vc9_norm6_vlc.table, VC9_NORM6_VLC_BITS, 2);
                if(code<0){
                    av_log(v->s.avctx, AV_LOG_DEBUG, "invalid NORM-6 VLC\n");
                    return -1;
                }
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
        decode_colskip(bp->data  ,             x, bp->height         , bp->stride, &v->s.gb);
        decode_rowskip(bp->data+x, bp->width - x, bp->height % tile_h, bp->stride, &v->s.gb);

        break;
    case IMODE_ROWSKIP:
        decode_rowskip(bp->data, bp->width, bp->height, bp->stride, &v->s.gb);
        break;
    case IMODE_COLSKIP:
        decode_colskip(bp->data, bp->width, bp->height, bp->stride, &v->s.gb);
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
/** @} */ //Bitplane group

/***********************************************************************/
/** VOP Dquant decoding
 * @param v VC9 Context
 */
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

/***********************************************************************/
/** 
 * @defgroup all_frame_hdr All VC9 profiles frame header
 * @brief Part of the frame header decoding from all profiles
 * @warning Only pro/epilog differs between Simple/Main and Advanced => check caller
 * @{
 */
/** B and BI frame header decoding, primary part
 * @see Tables 11+12, p62-65
 * @param v VC9 context
 * @return Status
 * @warning Also handles BI frames
 */
static int decode_b_picture_primary_header(VC9Context *v)
{
    GetBitContext *gb = &v->s.gb;
    int pqindex;

    /* Prolog common to all frametypes should be done in caller */
    if (v->profile == PROFILE_SIMPLE)
    {
        av_log(v->s.avctx, AV_LOG_ERROR, "Found a B frame while in Simple Profile!\n");
        return FRAME_SKIPPED;
    }
    v->bfraction = vc9_bfraction_lut[get_vlc2(gb, vc9_bfraction_vlc.table,
                                              VC9_BFRACTION_VLC_BITS, 2)];
    if (v->bfraction < -1)
    {
        av_log(v->s.avctx, AV_LOG_ERROR, "Invalid BFRaction\n");
        return FRAME_SKIPPED;
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
#if HAS_ADVANCED_PROFILE
    if (v->profile == PROFILE_ADVANCED)
    {
        if (v->postprocflag) v->postproc = get_bits(gb, 2);
        if (v->extended_mv == 1 && v->s.pict_type != BI_TYPE)
            v->mvrange = get_prefix(gb, 0, 3);
    }
#endif
    else
    {
        if (v->extended_mv == 1)
            v->mvrange = get_prefix(gb, 0, 3);
    }
    /* Read the MV mode */
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

/** B and BI frame header decoding, secondary part
 * @see Tables 11+12, p62-65
 * @param v VC9 context
 * @return Status
 * @warning Also handles BI frames
 * @warning To call once all MB arrays are allocated
 * @todo Support Advanced Profile headers
 */
static int decode_b_picture_secondary_header(VC9Context *v)
{
    GetBitContext *gb = &v->s.gb;
    int status;

    status = bitplane_decoding(&v->skip_mb_plane, v);
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

/** I frame header decoding, primary part
 * @see Tables 5+7, p53-54 and 55-57
 * @param v VC9 context
 * @return Status
 * @todo Support Advanced Profile headers
 */
static int decode_i_picture_primary_header(VC9Context *v)
{
    GetBitContext *gb = &v->s.gb;
    int pqindex;

    /* Prolog common to all frametypes should be done in caller */
    //BF = Buffer Fullness
    if (v->profile < PROFILE_ADVANCED && get_bits(gb, 7))
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
    return 0;
}

/** I frame header decoding, secondary part
 * @param v VC9 context
 * @return Status
 * @warning Not called in A/S/C profiles, it seems
 * @todo Support Advanced Profile headers
 */
static int decode_i_picture_secondary_header(VC9Context *v)
{
#if HAS_ADVANCED_PROFILE
    int status;
    if (v->profile == PROFILE_ADVANCED)
    {
        v->s.ac_pred = get_bits(&v->s.gb, 1);
        if (v->postprocflag) v->postproc = get_bits(&v->s.gb, 1);
        /* 7.1.1.34 + 8.5.2 */
        if (v->overlap && v->pq<9)
        {
            v->condover = get_bits(&v->s.gb, 1);
            if (v->condover)
            {
                v->condover = 2+get_bits(&v->s.gb, 1);
                if (v->condover == 3)
                {
                    status = bitplane_decoding(&v->over_flags_plane, v);
                    if (status < 0) return -1;
#  if TRACE
                    av_log(v->s.avctx, AV_LOG_DEBUG, "Overflags plane encoding: "
                           "Imode: %i, Invert: %i\n", status>>1, status&1);
#  endif
                }
            }
        }
    }
#endif

    /* Epilog (AC/DC syntax) should be done in caller */
    return 0;
}

/** P frame header decoding, primary part
 * @see Tables 5+7, p53-54 and 55-57
 * @param v VC9 context
 * @todo Support Advanced Profile headers
 * @return Status
 */
static int decode_p_picture_primary_header(VC9Context *v)
{
    /* INTERFRM, FRMCNT, RANGEREDFRM read in caller */
    GetBitContext *gb = &v->s.gb;
    int lowquant, pqindex;

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
    if (v->profile == PROFILE_ADVANCED)
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

/** P frame header decoding, secondary part
 * @see Tables 5+7, p53-54 and 55-57
 * @param v VC9 context
 * @warning To call once all MB arrays are allocated
 * @return Status
 */
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
/** @} */ //End of group all_frm_hdr


/***********************************************************************/
/** 
 * @defgroup std_frame_hdr VC9 Simple/Main Profiles header decoding
 * @brief Part of the frame header decoding belonging to Simple/Main Profiles
 * @warning Only pro/epilog differs between Simple/Main and Advanced =>
 *          check caller
 * @{
 */

/** Frame header decoding, first part, in Simple and Main profiles
 * @see Tables 5+7, p53-54 and 55-57
 * @param v VC9 context
 * @todo FIXME: RANGEREDFRM element not read if BI frame from Table6, P54
 *              However, 7.1.1.8 says "all frame types, for main profiles"
 * @return Status
 */
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
    case I_TYPE: status = decode_i_picture_primary_header(v); break;
    case P_TYPE: status = decode_p_picture_primary_header(v); break;
    case BI_TYPE: //Same as B
    case B_TYPE: status = decode_b_picture_primary_header(v); break;
    }

    if (status == FRAME_SKIPPED)
    {
      av_log(v->s.avctx, AV_LOG_INFO, "Skipping frame...\n");
      return status;
    }
    return 0;
}

/** Frame header decoding, secondary part
 * @param v VC9 context
 * @warning To call once all MB arrays are allocated
 * @return Status
 */
static int standard_decode_picture_secondary_header(VC9Context *v)
{
    GetBitContext *gb = &v->s.gb;
    int status = 0;

    switch (v->s.pict_type)
    {
    case P_TYPE: status = decode_p_picture_secondary_header(v); break;
    case B_TYPE: status = decode_b_picture_secondary_header(v); break;
    case BI_TYPE:
    case I_TYPE: break; //Nothing needed as it's done in the epilog
    }
    if (status < 0) return FRAME_SKIPPED;

    /* AC Syntax */
    v->c_ac_table_index = decode012(gb);
    if (v->s.pict_type == I_TYPE || v->s.pict_type == BI_TYPE)
    {
        v->y_ac_table_index = decode012(gb);
    }
    /* DC Syntax */
    v->s.dc_table_index = decode012(gb);

    return 0;
}
/** @} */ //End for group std_frame_hdr

#if HAS_ADVANCED_PROFILE
/***********************************************************************/
/** 
 * @defgroup adv_frame_hdr VC9 Advanced Profile header decoding
 * @brief Part of the frame header decoding belonging to Advanced Profiles
 * @warning Only pro/epilog differs between Simple/Main and Advanced =>
 *          check caller
 * @{
 */
/** Frame header decoding, primary part 
 * @param v VC9 context
 * @return Status
 */
static int advanced_decode_picture_primary_header(VC9Context *v)
{
    GetBitContext *gb = &v->s.gb;
    static const int type_table[4] = { P_TYPE, B_TYPE, I_TYPE, BI_TYPE };
    int type;

    if (v->interlace)
    {
        v->fcm = get_bits(gb, 1);
        if (v->fcm) v->fcm = 2+get_bits(gb, 1);
    }

    type = get_prefix(gb, 0, 4);
    if (type > 4 || type < 0) return FRAME_SKIPPED;
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
    case I_TYPE: if (decode_i_picture_primary_header(v) < 0) return -1;
    case P_TYPE: if (decode_p_picture_primary_header(v) < 0) return -1;
    case BI_TYPE:
    case B_TYPE: if (decode_b_picture_primary_header(v) < 0) return FRAME_SKIPPED;
    default: return -1;
    }
}

/** Frame header decoding, secondary part
 * @param v VC9 context
 * @return Status
 */
static int advanced_decode_picture_secondary_header(VC9Context *v)
{
    GetBitContext *gb = &v->s.gb;
    int status = 0;

    switch(v->s.pict_type)
    {
    case P_TYPE: status = decode_p_picture_secondary_header(v); break;
    case B_TYPE: status = decode_b_picture_secondary_header(v); break;
    case BI_TYPE:
    case I_TYPE: status = decode_i_picture_secondary_header(v); break; 
    }
    if (status<0) return FRAME_SKIPPED;

    /* AC Syntax */
    v->c_ac_table_index = decode012(gb);
    if (v->s.pict_type == I_TYPE || v->s.pict_type == BI_TYPE)
    {
        v->y_ac_table_index = decode012(gb);
    }
    /* DC Syntax */
    v->s.dc_table_index = decode012(gb);

    return 0;
}
#endif
/** @} */ //End for adv_frame_hdr

/***********************************************************************/
/** 
 * @defgroup block VC9 Block-level functions
 * @see 7.1.4, p91 and 8.1.1.7, p(1)04
 * @todo TODO: Integrate to MpegEncContext facilities
 * @{
 */

/**
 * @def GET_MQUANT
 * @brief Get macroblock-level quantizer scale
 * @warning XXX: qdiff to the frame quant, not previous quant ?
 * @fixme XXX: Don't know how to initialize mquant otherwise in last case
 */
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
    else mquant = v->pq;                                       \
  }

/**
 * @def GET_MVDATA(_dmv_x, _dmv_y)
 * @brief Get MV differentials
 * @see MVDATA decoding from 8.3.5.2, p(1)20
 * @param _dmv_x Horizontal differential for decoded MV
 * @param _dmv_y Vertical differential for decoded MV
 * @todo TODO: Use MpegEncContext arrays to store them
 */
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
    _dmv_x = get_bits(gb, v->k_x);                                  \
    _dmv_y = get_bits(gb, v->k_y);                                  \
    s->mb_intra = 1;                                                \
  }                                                                 \
  else                                                              \
  {                                                                 \
    index1 = index%6;                                               \
    if (s->mspel && index1 == 5) val = 1;                           \
    else                         val = 0;                           \
    val = get_bits(gb, size_table[index1] - val);                   \
    sign = 0 - (val&1);                                             \
    _dmv_x = (sign ^ ((val>>1) + offset_table[index1])) - sign;     \
                                                                    \
    index1 = index/6;                                               \
    if (s->mspel && index1 == 5) val = 1;                           \
    else                          val = 0;                          \
    val = get_bits(gb, size_table[index1] - val);                   \
    sign = 0 - (val&1);                                             \
    _dmv_y = (sign ^ ((val>>1) + offset_table[index1])) - sign;     \
  }

/** Get predicted DC value
 * prediction dir: left=0, top=1
 * @param s MpegEncContext
 * @param[in] n block index in the current MB
 * @param dc_val_ptr Pointer to DC predictor
 * @param dir_ptr Prediction direction for use in AC prediction
 * @todo TODO: Actually do it the VC9 way
 * @todo TODO: Handle properly edges
 */
static inline int vc9_pred_dc(MpegEncContext *s, int n,
                              uint16_t **dc_val_ptr, int *dir_ptr)
{
    int a, b, c, wrap, pred, scale;
    int16_t *dc_val;
    static const uint16_t dcpred[31] = {
        1024,  512,  341,  256,  205,  171,  146,  128, 
         114,  102,   93,   85,   79,   73,   68,   64, 
          60,   57,   54,   51,   49,   47,   45,   43, 
          41,   39,   38,   37,   35,   34,   33
    };

    /* find prediction - wmv3_dc_scale always used here in fact */
    if (n < 4)     scale = s->y_dc_scale;
    else           scale = s->c_dc_scale;

    wrap = s->block_wrap[n];
    dc_val= s->dc_val[0] + s->block_index[n];

    /* B C
     * A X 
     */
    a = dc_val[ - 1];
    b = dc_val[ - 1 - wrap];
    c = dc_val[ - wrap];

    /* XXX: Rule B is used only for I and BI frames in S/M/C profile
     *      with overlap filtering off 
     */
    if ((s->pict_type == I_TYPE || s->pict_type == BI_TYPE) &&
        1 /* XXX: overlap filtering off */)
    {
        /* Set outer values */
        if (s->first_slice_line && n!=2) b=c=dcpred[scale];
        if (s->mb_x == 0) b=a=dcpred[scale];
    }
    else
    {
        /* Set outer values */
        if (s->first_slice_line && n!=2) b=c=0;
        if (s->mb_x == 0) b=a=0;

        /* XXX: Rule A needs to know if blocks are inter or intra :/ */
        if (0)
        {
            /* update predictor */
            *dc_val_ptr = &dc_val[0];
            dir_ptr = 0;
            return a;
        }
    }

    if (abs(a - b) <= abs(b - c)) {
        pred = c;
        *dir_ptr = 1;
    } else {
        pred = a;
        *dir_ptr = 0;
    }

    /* update predictor */
    *dc_val_ptr = &dc_val[0];
    return pred;
}

/** Decode one block, inter or intra
 * @param v The VC9 context
 * @param block 8x8 DCT block
 * @param n Block index in the current MB (<4=>luma)
 * @param coded If the block is coded
 * @param mquant Quantizer step for the current block
 * @see Inter TT: Table 21, p73 + p91-85
 * @see Intra TT: Table 20, p72 + p(1)05-(1)07
 * @todo TODO: Process the blocks
 * @todo TODO: Use M$ MPEG-4 cbp prediction
 */
int vc9_decode_block(VC9Context *v, DCTELEM block[64], int n, int coded, int mquant)
{
    GetBitContext *gb = &v->s.gb;
    MpegEncContext *s = &v->s;
    int ttblk; /* Transform Type per Block */
    int subblkpat; /* Sub-block Transform Type Pattern */
    int dc_pred_dir; /* Direction of the DC prediction used */
    int run_diff, i;

    /* XXX: Guard against dumb values of mquant */
    mquant = (mquant < 1) ? 0 : ( (mquant>31) ? 31 : mquant );

    /* Set DC scale - y and c use the same */
    s->y_dc_scale = s->y_dc_scale_table[mquant];
    s->c_dc_scale = s->c_dc_scale_table[mquant];

    if (s->mb_intra)
    {
        int dcdiff;
        uint16_t *dc_val;

        /* Get DC differential */
        if (n < 4) {
            dcdiff = get_vlc2(&s->gb, ff_msmp4_dc_luma_vlc[s->dc_table_index].table, DC_VLC_BITS, 3);
        } else {
            dcdiff = get_vlc2(&s->gb, ff_msmp4_dc_chroma_vlc[s->dc_table_index].table, DC_VLC_BITS, 3);
        }
        if (dcdiff < 0){
            av_log(s->avctx, AV_LOG_ERROR, "Illegal DC VLC\n");
            return -1;
        }
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

        /* Prediction */
        dcdiff += vc9_pred_dc(s, n, &dc_val, &dc_pred_dir);
        *dc_val = dcdiff;
        /* Store the quantized DC coeff, used for prediction */

        if (n < 4) {
            block[0] = dcdiff * s->y_dc_scale;
        } else {
            block[0] = dcdiff * s->c_dc_scale;
        }
        if (block[0] < 0) {
#if TRACE
            //av_log(s->avctx, AV_LOG_ERROR, "DC=%i<0\n", dcdiff);
#endif
            //return -1;
        }
        /* Skip ? */
        run_diff = 0;
        i = 0;
        if (!coded) {
            goto not_coded;
        }
    }
    else
    {
        mquant = v->pq;

        /* Get TTBLK */
        if (v->ttmb < 8) /* per block */
            ttblk = get_vlc2(gb, vc9_ttblk_vlc[v->tt_index].table, VC9_TTBLK_VLC_BITS, 2);
        else /* Per frame */
          ttblk = 0; //FIXME, depends on ttfrm

        /* Get SUBBLKPAT */
        if (ttblk == v->ttblk4x4) /* 4x4 transform for that qp value */
            subblkpat = 1+get_vlc2(gb, vc9_subblkpat_vlc[v->tt_index].table,
                                   VC9_SUBBLKPAT_VLC_BITS, 2);
        else /* All others: 8x8, 4x8, 8x4 */
            subblkpat = decode012(gb);
    }

    //TODO AC Decoding
    i = 63; //XXX: nothing done yet


 not_coded:
    if (s->mb_intra) {
        mpeg4_pred_ac(s, block, n, dc_pred_dir);
        if (s->ac_pred) {
            i = 63; /* XXX: not optimal */
        }
    }
    if(i>0) i=63; //FIXME/XXX optimize
    s->block_last_index[n] = i;
    return 0;
}

/** @} */ //End for group block

/***********************************************************************/
/** 
 * @defgroup std_mb VC9 Macroblock-level functions in Simple/Main Profiles
 * @see 7.1.4, p91 and 8.1.1.7, p(1)04
 * @todo TODO: Integrate to MpegEncContext facilities
 * @{
 */

static inline int vc9_coded_block_pred(MpegEncContext * s, int n, uint8_t **coded_block_ptr)
{
    int xy, wrap, pred, a, b, c;

    xy = s->block_index[n];
    wrap = s->b8_stride;

    /* B C
     * A X 
     */
    a = s->coded_block[xy - 1       ];
    b = s->coded_block[xy - 1 - wrap];
    c = s->coded_block[xy     - wrap];

    if (b == c) {
        pred = a;
    } else {
        pred = c;
    }

    /* store value */
    *coded_block_ptr = &s->coded_block[xy];

    return pred;
}

/** Decode one I-frame MB (in Simple/Main profile)
 * @todo TODO: Extend to AP
 */
int vc9_decode_i_mb(VC9Context *v, DCTELEM block[6][64])
{
    int i, cbp, val;
    uint8_t *coded_val;
//    uint32_t * const mb_type_ptr= &v->s.current_picture.mb_type[ v->s.mb_x + v->s.mb_y*v->s.mb_stride ];

    v->s.mb_intra = 1;
    cbp = get_vlc2(&v->s.gb, ff_msmp4_mb_i_vlc.table, MB_INTRA_VLC_BITS, 2);
    if (cbp < 0) return -1;
    v->s.ac_pred = get_bits(&v->s.gb, 1);

    for (i=0; i<6; i++)
    {
        val = ((cbp >> (5 - i)) & 1);
        if (i < 4) {
            int pred = vc9_coded_block_pred(&v->s, i, &coded_val);
            val = val ^ pred;
            *coded_val = val;
        }
        cbp |= val << (5 - i);
        if (vc9_decode_block(v, block[i], i, val, v->pq) < 0) //FIXME Should be mquant
        {
            av_log(v->s.avctx, AV_LOG_ERROR,
                   "\nerror while decoding block: %d x %d (%d)\n", v->s.mb_x, v->s.mb_y, i);
            return -1;
        }
    }
    return 0;
}

/** Decode one P-frame MB (in Simple/Main profile)
 * @todo TODO: Extend to AP
 * @fixme FIXME: DC value for inter blocks not set
 */
int vc9_decode_p_mb(VC9Context *v, DCTELEM block[6][64])
{
    MpegEncContext *s = &v->s;
    GetBitContext *gb = &s->gb;
    int i, mb_offset = s->mb_x + s->mb_y*s->mb_width; /* XXX: mb_stride */
    int cbp; /* cbp decoding stuff */
    int hybrid_pred; /* Prediction types */
    int mv_mode_bit = 0; 
    int mqdiff, mquant; /* MB quantization */
    int ttmb; /* MB Transform type */
    int status;
    uint8_t *coded_val;

    static const int size_table[6] = { 0, 2, 3, 4, 5, 8 },
      offset_table[6] = { 0, 1, 3, 7, 15, 31 };
    int mb_has_coeffs = 1; /* last_flag */
    int dmv_x, dmv_y; /* Differential MV components */
    int index, index1; /* LUT indices */
    int val, sign; /* temp values */

    mquant = v->pq; /* Loosy initialization */

    if (v->mv_type_mb_plane.is_raw)
        v->mv_type_mb_plane.data[mb_offset] = get_bits(gb, 1);
    if (v->skip_mb_plane.is_raw)
        v->skip_mb_plane.data[mb_offset] = get_bits(gb, 1);
    if (!mv_mode_bit) /* 1MV mode */
    {
        if (!v->skip_mb_plane.data[mb_offset])
        {
            GET_MVDATA(dmv_x, dmv_y);

            /* hybrid mv pred, 8.3.5.3.4 */
            if (v->mv_mode == MV_PMODE_1MV ||
                v->mv_mode == MV_PMODE_MIXED_MV)
                hybrid_pred = get_bits(gb, 1);
            /* FIXME Set DC val for inter block ? */
            if (s->mb_intra && !mb_has_coeffs)
            {
                GET_MQUANT();
                s->ac_pred = get_bits(gb, 1);
                /* XXX: how to handle cbp ? */
                cbp = 0;
                for (i=0; i<6; i++)
                {
                     s->coded_block[s->block_index[i]] = 0;
                     vc9_decode_block(v, block[i], i, 0, mquant);
                }
                return 0;
            }
            else if (mb_has_coeffs)
            {
                if (s->mb_intra) s->ac_pred = get_bits(gb, 1);
                cbp = get_vlc2(&v->s.gb, v->cbpcy_vlc->table, VC9_CBPCY_P_VLC_BITS, 2);
                GET_MQUANT();
            }
            else
            {
                mquant = v->pq;
                /* XXX: how to handle cbp ? */
                /* XXX: how to set values for following predictions ? */
                cbp = 0;
            }

            if (!v->ttmbf)
                ttmb = get_vlc2(gb, vc9_ttmb_vlc[v->tt_index].table,
                                VC9_TTMB_VLC_BITS, 12);

            for (i=0; i<6; i++)
            {
                val = ((cbp >> (5 - i)) & 1);
                if (i < 4) {
                    int pred = vc9_coded_block_pred(&v->s, i, &coded_val);
                    val = val ^ pred;
                    *coded_val = val;
                }
                vc9_decode_block(v, block[i], i, val, mquant); //FIXME
            }
        }
        else //Skipped
        {
            /* hybrid mv pred, 8.3.5.3.4 */
            if (v->mv_mode == MV_PMODE_1MV ||
                v->mv_mode == MV_PMODE_MIXED_MV)
                hybrid_pred = get_bits(gb, 1);

            /* TODO: blah */
            return 0;
        }
    } //1MV mode
    else //4MV mode
    {
        if (!v->skip_mb_plane.data[mb_offset] /* unskipped MB */)
        {
            /* Get CBPCY */
            cbp = get_vlc2(&v->s.gb, v->cbpcy_vlc->table, VC9_CBPCY_P_VLC_BITS, 2);
            for (i=0; i<6; i++)
            {
                val = ((cbp >> (5 - i)) & 1);
                if (i < 4) {
                    int pred = vc9_coded_block_pred(&v->s, i, &coded_val);
                    val = val ^ pred;
                    *coded_val = val;
                }
                if (i<4 && val)
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
                    ttmb = get_vlc2(gb, vc9_ttmb_vlc[v->tt_index].table,
                                    VC9_TTMB_VLC_BITS, 12);
                status = vc9_decode_block(v, block[i], i, val, mquant);
            }
            return status;
        }
        else //Skipped MB
        {
            /* XXX: Skipped => cbp=0 and mquant doesn't matter ? */
            for (i=0; i<4; i++)
            {
                if (v->mv_mode == MV_PMODE_MIXED_MV /* Hybrid pred */)
                    hybrid_pred = get_bits(gb, 1);
                vc9_decode_block(v, block[i], i, 0, v->pq); //FIXME
            }
            vc9_decode_block(v, block[4], 4, 0, v->pq); //FIXME
            vc9_decode_block(v, block[5], 5, 0, v->pq); //FIXME
            /* TODO: blah */
            return 0;
        }
    }

    /* Should never happen */
    return -1;
}

/** Decode one B-frame MB (in Simple/Main profile)
 * @todo TODO: Extend to AP
 * @warning XXX: Used for decoding BI MBs
 * @fixme FIXME: DC value for inter blocks not set
 */
int vc9_decode_b_mb(VC9Context *v, DCTELEM block[6][64])
{
    MpegEncContext *s = &v->s;
    GetBitContext *gb = &v->s.gb;
    int mb_offset, i /* MB / B postion information */;
    int b_mv_type = BMV_TYPE_BACKWARD;
    int mquant, mqdiff; /* MB quant stuff */
    int ttmb; /* MacroBlock transform type */

    static const int size_table[6] = { 0, 2, 3, 4, 5, 8 },
        offset_table[6] = { 0, 1, 3, 7, 15, 31 };
    int mb_has_coeffs = 1; /* last_flag */
    int dmv1_x, dmv1_y, dmv2_x, dmv2_y; /* Differential MV components */
    int index, index1; /* LUT indices */
    int val, sign; /* MVDATA temp values */

    mb_offset = s->mb_width*s->mb_y + s->mb_x; //FIXME: arrays aren't using stride

    if (v->direct_mb_plane.is_raw)
        v->direct_mb_plane.data[mb_offset] = get_bits(gb, 1);
    if (v->skip_mb_plane.is_raw)
        v->skip_mb_plane.data[mb_offset] = get_bits(gb, 1);

    if (!v->direct_mb_plane.data[mb_offset])
    {
        if (v->skip_mb_plane.data[mb_offset])
        {
            b_mv_type = decode012(gb);
            if (v->bfraction > 420 /*1/2*/ &&
                b_mv_type < 3) b_mv_type = 1-b_mv_type;
        }
        else
        {
            GET_MVDATA(dmv1_x, dmv1_y);
            if (!s->mb_intra /* b_mv1 tells not intra */)
            {
                b_mv_type = decode012(gb);
                if (v->bfraction > 420 /*1/2*/ &&
                    b_mv_type < 3) b_mv_type = 1-b_mv_type;
            }
        }
    }
    if (!v->skip_mb_plane.data[mb_offset])
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
                mquant = v->pq; //FIXME: initialization not necessary ?
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
        ttmb = get_vlc2(gb, vc9_ttmb_vlc[v->tt_index].table,
                        VC9_TTMB_VLC_BITS, 12);

    //End2
    for (i=0; i<6; i++)
    {
        vc9_decode_block(v, block[i], i, 0 /*cbp[i]*/, mquant); //FIXME
    }
    return 0;
}

/** Decode all MBs for an I frame in Simple/Main profile
 * @todo TODO: Move out of the loop the picture type case?
               (branch prediction should help there though)
 */
static int standard_decode_mbs(VC9Context *v)
{
    MpegEncContext *s = &v->s;

    /* Set transform type info depending on pq */
    if (v->pq < 5)
    {
        v->tt_index = 0;
        v->ttblk4x4 = 3;
    }
    else if (v->pq < 13)
    {
        v->tt_index = 1;
        v->ttblk4x4 = 3;
    }
    else
    {
        v->tt_index = 2;
        v->ttblk4x4 = 2;
    }

    if (s->pict_type != I_TYPE)
    {
        /* Select proper long MV range */
        switch (v->mvrange)
        {
        case 1: v->k_x = 10; v->k_y = 9; break;
        case 2: v->k_x = 12; v->k_y = 10; break;
        case 3: v->k_x = 13; v->k_y = 11; break;
        default: /*case 0 too */ v->k_x = 9; v->k_y = 8; break;
        }

        s->mspel = v->mv_mode & 1; //MV_PMODE is HPEL
        v->k_x -= s->mspel;
        v->k_y -= s->mspel;
    }

    for (s->mb_y=0; s->mb_y<s->mb_height; s->mb_y++)
    {
        for (s->mb_x=0; s->mb_x<s->mb_width; s->mb_x++)
        {
            //FIXME Get proper MB DCTELEM
            //TODO Move out of the loop
            switch (s->pict_type)
            {
            case I_TYPE: vc9_decode_i_mb(v, s->block); break;
            case P_TYPE: vc9_decode_p_mb(v, s->block); break;
            case BI_TYPE:
            case B_TYPE: vc9_decode_b_mb(v, s->block); break;
            }
        }
        //Add a check for overconsumption ?
    }
    return 0;
}
/** @} */ //End for group std_mb

#if HAS_ADVANCED_PROFILE
/***********************************************************************/
/** 
 * @defgroup adv_mb VC9 Macroblock-level functions in Advanced Profile
 * @todo TODO: Integrate to MpegEncContext facilities
 * @todo TODO: Code P, B and BI
 * @{
 */
static int advanced_decode_i_mbs(VC9Context *v)
{
    MpegEncContext *s = &v->s;
    GetBitContext *gb = &v->s.gb;
    int mqdiff, mquant, mb_offset = 0, over_flags_mb = 0;

    for (s->mb_y=0; s->mb_y<s->mb_height; s->mb_y++)
    {
        for (s->mb_x=0; s->mb_x<s->mb_width; s->mb_x++)
        {
            if (v->ac_pred_plane.is_raw)
                s->ac_pred = get_bits(gb, 1);
            else
                s->ac_pred = v->ac_pred_plane.data[mb_offset];
            if (v->condover == 3 && v->over_flags_plane.is_raw)
                over_flags_mb = get_bits(gb, 1);
            GET_MQUANT();

            /* TODO: lots */
        }
        mb_offset++;
    }
    return 0;
}
/** @} */ //End for group adv_mb
#endif

/** Initialize a VC9/WMV3 decoder
 * @todo TODO: Handle VC-9 IDUs (Transport level?)
 * @todo TODO: Decypher remaining bits in extra_data
 */
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

    av_log(avctx, AV_LOG_INFO, "This decoder is not supposed to produce picture. Dont report this as a bug!\n");

    avctx->coded_width = avctx->width;
    avctx->coded_height = avctx->height;
    if (avctx->codec_id == CODEC_ID_WMV3)
    {
        int count = 0;

        // looks like WMV3 has a sequence header stored in the extradata
        // advanced sequence header may be before the first frame
        // the last byte of the extradata is a version number, 1 for the
        // samples we can decode

        init_get_bits(&gb, avctx->extradata, avctx->extradata_size*8);

        if (decode_sequence_header(avctx, &gb) < 0)
          return -1;

        count = avctx->extradata_size*8 - get_bits_count(&gb);
        if (count>0)
        {
            av_log(avctx, AV_LOG_INFO, "Extra data: %i bits left, value: %X\n",
                   count, get_bits(&gb, count));
        }
        else if (count < 0)
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
    if (v->profile == PROFILE_ADVANCED)
    {
        if (alloc_bitplane(&v->over_flags_plane, s->mb_width, s->mb_height) < 0)
            return -1;
        if (alloc_bitplane(&v->ac_pred_plane, s->mb_width, s->mb_height) < 0)
            return -1;
    }
#endif

    return 0;
    }

/** Decode a VC9/WMV3 frame
 * @todo TODO: Handle VC-9 IDUs (Transport level?)
 * @warning Initial try at using MpegEncContext stuff
 */
static int vc9_decode_frame(AVCodecContext *avctx,
                            void *data, int *data_size,
                            uint8_t *buf, int buf_size)
{
    VC9Context *v = avctx->priv_data;
    MpegEncContext *s = &v->s;
    int ret = FRAME_SKIPPED, len;
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
                if (v->profile < PROFILE_ADVANCED)
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
    if (v->profile == PROFILE_ADVANCED)
        ret= advanced_decode_picture_primary_header(v);
    else
#endif
        ret= standard_decode_picture_primary_header(v);
    if (ret == FRAME_SKIPPED) return buf_size;
    /* skip if the header was thrashed */
    if (ret < 0){
        av_log(s->avctx, AV_LOG_ERROR, "header damaged\n");
        return -1;
    }

    //No bug workaround yet, no DCT conformance

    //WMV9 does have resized images
    if (v->profile < PROFILE_ADVANCED && v->multires){
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
    if (v->profile == PROFILE_ADVANCED)
        ret= advanced_decode_picture_secondary_header(v);
    else
#endif
        ret = standard_decode_picture_secondary_header(v);
    if (ret<0) return FRAME_SKIPPED; //FIXME Non fatal for now

    //We consider the image coded in only one slice
#if HAS_ADVANCED_PROFILE
    if (v->profile == PROFILE_ADVANCED)
    {
        switch(s->pict_type)
        {
            case I_TYPE: ret = advanced_decode_i_mbs(v); break;
            case P_TYPE: ret = decode_p_mbs(v); break;
            case B_TYPE:
            case BI_TYPE: ret = decode_b_mbs(v); break;
            default: ret = FRAME_SKIPPED;
        }
        if (ret == FRAME_SKIPPED) return buf_size; //We ignore for now failures
    }
    else
#endif
    {
        ret = standard_decode_mbs(v);
        if (ret == FRAME_SKIPPED) return buf_size;
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

/** Close a VC9/WMV3 decoder
 * @warning Initial try at using MpegEncContext stuff
 */
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
