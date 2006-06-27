/*
 * VC-1 and WMV3 decoder
 * Copyright (c) 2006 Konstantin Shishkov
 * Partly based on vc9.c (c) 2005 Anonymous, Alex Beregszaszi, Michael Niedermayer
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

/**
 * @file vc1.c
 * VC-1 and WMV3 decoder
 *
 */
#include "common.h"
#include "dsputil.h"
#include "avcodec.h"
#include "mpegvideo.h"
#include "vc1data.h"
#include "vc1acdata.h"

#undef NDEBUG
#include <assert.h>

extern const uint32_t ff_table0_dc_lum[120][2], ff_table1_dc_lum[120][2];
extern const uint32_t ff_table0_dc_chroma[120][2], ff_table1_dc_chroma[120][2];
extern VLC ff_msmp4_dc_luma_vlc[2], ff_msmp4_dc_chroma_vlc[2];
#define MB_INTRA_VLC_BITS 9
extern VLC ff_msmp4_mb_i_vlc;
extern const uint16_t ff_msmp4_mb_i_table[64][2];
#define DC_VLC_BITS 9
#define AC_VLC_BITS 9
static const uint16_t table_mb_intra[64][2];


/** Available Profiles */
//@{
enum Profile {
    PROFILE_SIMPLE,
    PROFILE_MAIN,
    PROFILE_COMPLEX, ///< TODO: WMV9 specific
    PROFILE_ADVANCED
};
//@}

/** Sequence quantizer mode */
//@{
enum QuantMode {
    QUANT_FRAME_IMPLICIT,    ///< Implicitly specified at frame level
    QUANT_FRAME_EXPLICIT,    ///< Explicitly specified at frame level
    QUANT_NON_UNIFORM,       ///< Non-uniform quant used for all frames
    QUANT_UNIFORM            ///< Uniform quant used for all frames
};
//@}

/** Where quant can be changed */
//@{
enum DQProfile {
    DQPROFILE_FOUR_EDGES,
    DQPROFILE_DOUBLE_EDGES,
    DQPROFILE_SINGLE_EDGE,
    DQPROFILE_ALL_MBS
};
//@}

/** @name Where quant can be changed
 */
//@{
enum DQSingleEdge {
    DQSINGLE_BEDGE_LEFT,
    DQSINGLE_BEDGE_TOP,
    DQSINGLE_BEDGE_RIGHT,
    DQSINGLE_BEDGE_BOTTOM
};
//@}

/** Which pair of edges is quantized with ALTPQUANT */
//@{
enum DQDoubleEdge {
    DQDOUBLE_BEDGE_TOPLEFT,
    DQDOUBLE_BEDGE_TOPRIGHT,
    DQDOUBLE_BEDGE_BOTTOMRIGHT,
    DQDOUBLE_BEDGE_BOTTOMLEFT
};
//@}

/** MV modes for P frames */
//@{
enum MVModes {
    MV_PMODE_1MV_HPEL_BILIN,
    MV_PMODE_1MV,
    MV_PMODE_1MV_HPEL,
    MV_PMODE_MIXED_MV,
    MV_PMODE_INTENSITY_COMP
};
//@}

/** @name MV types for B frames */
//@{
enum BMVTypes {
    BMV_TYPE_BACKWARD,
    BMV_TYPE_FORWARD,
    BMV_TYPE_INTERPOLATED = 3 //XXX: ??
};
//@}

/** @name Block types for P/B frames */
//@{
enum TransformTypes {
    TT_8X8,
    TT_8X4_BOTTOM,
    TT_8X4_TOP,
    TT_8X4, //Both halves
    TT_4X8_RIGHT,
    TT_4X8_LEFT,
    TT_4X8, //Both halves
    TT_4X4
};
//@}

/** Table for conversion between TTBLK and TTMB */
static const int ttblk_to_tt[3][8] = {
  { TT_8X4, TT_4X8, TT_8X8, TT_4X4, TT_8X4_TOP, TT_8X4_BOTTOM, TT_4X8_RIGHT, TT_4X8_LEFT },
  { TT_8X8, TT_4X8_RIGHT, TT_4X8_LEFT, TT_4X4, TT_8X4, TT_4X8, TT_8X4_BOTTOM, TT_8X4_TOP },
  { TT_8X8, TT_4X8, TT_4X4, TT_8X4_BOTTOM, TT_4X8_RIGHT, TT_4X8_LEFT, TT_8X4, TT_8X4_TOP }
};

/** MV P mode - the 5th element is only used for mode 1 */
static const uint8_t mv_pmode_table[2][5] = {
  { MV_PMODE_1MV_HPEL_BILIN, MV_PMODE_1MV, MV_PMODE_1MV_HPEL, MV_PMODE_INTENSITY_COMP, MV_PMODE_MIXED_MV },
  { MV_PMODE_1MV, MV_PMODE_MIXED_MV, MV_PMODE_1MV_HPEL, MV_PMODE_INTENSITY_COMP, MV_PMODE_1MV_HPEL_BILIN }
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

/** @name VC-1 VLC tables and defines
 *  @todo TODO move this into the context
 */
//@{
#define VC1_BFRACTION_VLC_BITS 7
static VLC vc1_bfraction_vlc;
#define VC1_IMODE_VLC_BITS 4
static VLC vc1_imode_vlc;
#define VC1_NORM2_VLC_BITS 3
static VLC vc1_norm2_vlc;
#define VC1_NORM6_VLC_BITS 9
static VLC vc1_norm6_vlc;
/* Could be optimized, one table only needs 8 bits */
#define VC1_TTMB_VLC_BITS 9 //12
static VLC vc1_ttmb_vlc[3];
#define VC1_MV_DIFF_VLC_BITS 9 //15
static VLC vc1_mv_diff_vlc[4];
#define VC1_CBPCY_P_VLC_BITS 9 //14
static VLC vc1_cbpcy_p_vlc[4];
#define VC1_4MV_BLOCK_PATTERN_VLC_BITS 6
static VLC vc1_4mv_block_pattern_vlc[4];
#define VC1_TTBLK_VLC_BITS 5
static VLC vc1_ttblk_vlc[3];
#define VC1_SUBBLKPAT_VLC_BITS 6
static VLC vc1_subblkpat_vlc[3];

static VLC vc1_ac_coeff_table[8];
//@}

enum CodingSet {
    CS_HIGH_MOT_INTRA = 0,
    CS_HIGH_MOT_INTER,
    CS_LOW_MOT_INTRA,
    CS_LOW_MOT_INTER,
    CS_MID_RATE_INTRA,
    CS_MID_RATE_INTER,
    CS_HIGH_RATE_INTRA,
    CS_HIGH_RATE_INTER
};

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


/** Block data for DC/AC prediction
*/
typedef struct Block {
    uint16_t dc;
    int16_t hor_ac[7];
    int16_t vert_ac[7];
    int16_t dcstep, step;
} Block;

/** The VC1 Context
 * @fixme Change size wherever another size is more efficient
 * Many members are only used for Advanced Profile
 */
typedef struct VC1Context{
    MpegEncContext s;

    int bits;

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
    int range_x, range_y; ///< MV range
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
    int codingset;        ///< index of current table set from 11.8 to use for luma block decoding
    int codingset2;       ///< index of current table set from 11.8 to use for chroma block decoding
    int pqindex;          ///< raw pqindex used in coding set selection
    int a_avail, c_avail;


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
} VC1Context;

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
    int i;

    for(i = 0; i < len && get_bits1(gb) != stop; i++);
    return i;
/*  int i = 0, tmp = !stop;

  while (i != len && tmp != stop)
  {
    tmp = get_bits(gb, 1);
    i++;
  }
  if (i == len && tmp != stop) return len+1;
  return i;*/
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

static inline int decode210(GetBitContext *gb){
    int n;
    n = get_bits1(gb);
    if (n == 1)
        return 0;
    else
        return 2 - get_bits1(gb);
}

/**
 * Init VC-1 specific tables and VC1Context members
 * @param v The VC1Context to initialize
 * @return Status
 */
static int vc1_init_common(VC1Context *v)
{
    static int done = 0;
    int i = 0;

    /* Set the bit planes */
    v->mv_type_mb_plane = (struct BitPlane) { NULL, 0, 0, 0 };
    v->direct_mb_plane = (struct BitPlane) { NULL, 0, 0, 0 };
    v->skip_mb_plane = (struct BitPlane) { NULL, 0, 0, 0 };
    v->ac_pred_plane = v->over_flags_plane = (struct BitPlane) { NULL, 0, 0, 0 };
    v->hrd_rate = v->hrd_buffer = NULL;

    /* VLC tables */
    if(!done)
    {
        done = 1;
        init_vlc(&vc1_bfraction_vlc, VC1_BFRACTION_VLC_BITS, 23,
                 vc1_bfraction_bits, 1, 1,
                 vc1_bfraction_codes, 1, 1, 1);
        init_vlc(&vc1_norm2_vlc, VC1_NORM2_VLC_BITS, 4,
                 vc1_norm2_bits, 1, 1,
                 vc1_norm2_codes, 1, 1, 1);
        init_vlc(&vc1_norm6_vlc, VC1_NORM6_VLC_BITS, 64,
                 vc1_norm6_bits, 1, 1,
                 vc1_norm6_codes, 2, 2, 1);
        init_vlc(&vc1_imode_vlc, VC1_IMODE_VLC_BITS, 7,
                 vc1_imode_bits, 1, 1,
                 vc1_imode_codes, 1, 1, 1);
        for (i=0; i<3; i++)
        {
            init_vlc(&vc1_ttmb_vlc[i], VC1_TTMB_VLC_BITS, 16,
                     vc1_ttmb_bits[i], 1, 1,
                     vc1_ttmb_codes[i], 2, 2, 1);
            init_vlc(&vc1_ttblk_vlc[i], VC1_TTBLK_VLC_BITS, 8,
                     vc1_ttblk_bits[i], 1, 1,
                     vc1_ttblk_codes[i], 1, 1, 1);
            init_vlc(&vc1_subblkpat_vlc[i], VC1_SUBBLKPAT_VLC_BITS, 15,
                     vc1_subblkpat_bits[i], 1, 1,
                     vc1_subblkpat_codes[i], 1, 1, 1);
        }
        for(i=0; i<4; i++)
        {
            init_vlc(&vc1_4mv_block_pattern_vlc[i], VC1_4MV_BLOCK_PATTERN_VLC_BITS, 16,
                     vc1_4mv_block_pattern_bits[i], 1, 1,
                     vc1_4mv_block_pattern_codes[i], 1, 1, 1);
            init_vlc(&vc1_cbpcy_p_vlc[i], VC1_CBPCY_P_VLC_BITS, 64,
                     vc1_cbpcy_p_bits[i], 1, 1,
                     vc1_cbpcy_p_codes[i], 2, 2, 1);
            init_vlc(&vc1_mv_diff_vlc[i], VC1_MV_DIFF_VLC_BITS, 73,
                     vc1_mv_diff_bits[i], 1, 1,
                     vc1_mv_diff_codes[i], 2, 2, 1);
        }
        for(i=0; i<8; i++)
            init_vlc(&vc1_ac_coeff_table[i], AC_VLC_BITS, vc1_ac_sizes[i],
                     &vc1_ac_tables[i][0][1], 8, 4,
                     &vc1_ac_tables[i][0][0], 8, 4, 1);
        init_vlc(&ff_msmp4_mb_i_vlc, MB_INTRA_VLC_BITS, 64,
                 &ff_msmp4_mb_i_table[0][1], 4, 2,
                 &ff_msmp4_mb_i_table[0][0], 4, 2, 1);
    }

    /* Other defaults */
    v->pq = -1;
    v->mvrange = 0; /* 7.1.1.18, p80 */

    return 0;
}

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
enum Imode {
    IMODE_RAW,
    IMODE_NORM2,
    IMODE_DIFF2,
    IMODE_NORM6,
    IMODE_DIFF6,
    IMODE_ROWSKIP,
    IMODE_COLSKIP
};
/** @} */ //imode defines

/** Allocate the buffer from a bitplane, given its dimensions
 * @param bp Bitplane which buffer is to allocate
 * @param[in] width Width of the buffer
 * @param[in] height Height of the buffer
 * @return Status
 * @todo TODO: Take into account stride
 * @todo TODO: Allow use of external buffers ?
 */
static int alloc_bitplane(BitPlane *bp, int width, int height)
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
static void free_bitplane(BitPlane *bp)
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
 * @param v VC-1 context for bit reading and logging
 * @return Status
 * @fixme FIXME: Optimize
 * @todo TODO: Decide if a struct is needed
 */
static int bitplane_decoding(BitPlane *bp, VC1Context *v)
{
    GetBitContext *gb = &v->s.gb;

    int imode, x, y, code, offset;
    uint8_t invert, *planep = bp->data;

    invert = get_bits(gb, 1);
    imode = get_vlc2(gb, vc1_imode_vlc.table, VC1_IMODE_VLC_BITS, 1);

    bp->is_raw = 0;
    switch (imode)
    {
    case IMODE_RAW:
        //Data is actually read in the MB layer (same for all tests == "raw")
        bp->is_raw = 1; //invert ignored
        return invert;
    case IMODE_DIFF2:
    case IMODE_NORM2:
        if ((bp->height * bp->width) & 1)
        {
            *planep++ = get_bits(gb, 1);
            offset = 1;
        }
        else offset = 0;
        // decode bitplane as one long line
        for (y = offset; y < bp->height * bp->width; y += 2) {
            code = get_vlc2(gb, vc1_norm2_vlc.table, VC1_NORM2_VLC_BITS, 1);
            *planep++ = code & 1;
            offset++;
            if(offset == bp->width) {
                offset = 0;
                planep += bp->stride - bp->width;
            }
            *planep++ = code >> 1;
            offset++;
            if(offset == bp->width) {
                offset = 0;
                planep += bp->stride - bp->width;
            }
        }
        break;
    case IMODE_DIFF6:
    case IMODE_NORM6:
        if(!(bp->height % 3) && (bp->width % 3)) { // use 2x3 decoding
            for(y = 0; y < bp->height; y+= 3) {
                for(x = bp->width & 1; x < bp->width; x += 2) {
                    code = get_vlc2(gb, vc1_norm6_vlc.table, VC1_NORM6_VLC_BITS, 2);
                    if(code < 0){
                        av_log(v->s.avctx, AV_LOG_DEBUG, "invalid NORM-6 VLC\n");
                        return -1;
                    }
                    planep[x + 0] = (code >> 0) & 1;
                    planep[x + 1] = (code >> 1) & 1;
                    planep[x + 0 + bp->stride] = (code >> 2) & 1;
                    planep[x + 1 + bp->stride] = (code >> 3) & 1;
                    planep[x + 0 + bp->stride * 2] = (code >> 4) & 1;
                    planep[x + 1 + bp->stride * 2] = (code >> 5) & 1;
                }
                planep += bp->stride * 3;
            }
            if(bp->width & 1) decode_colskip(bp->data, 1, bp->height, bp->stride, &v->s.gb);
        } else { // 3x2
            for(y = bp->height & 1; y < bp->height; y += 2) {
                for(x = bp->width % 3; x < bp->width; x += 3) {
                    code = get_vlc2(gb, vc1_norm6_vlc.table, VC1_NORM6_VLC_BITS, 2);
                    if(code < 0){
                        av_log(v->s.avctx, AV_LOG_DEBUG, "invalid NORM-6 VLC\n");
                        return -1;
                    }
                    planep[x + 0] = (code >> 0) & 1;
                    planep[x + 1] = (code >> 1) & 1;
                    planep[x + 2] = (code >> 2) & 1;
                    planep[x + 0 + bp->stride] = (code >> 3) & 1;
                    planep[x + 1 + bp->stride] = (code >> 4) & 1;
                    planep[x + 2 + bp->stride] = (code >> 5) & 1;
                }
                planep += bp->stride * 2;
            }
            x = bp->width % 3;
            if(x) decode_colskip(bp->data  ,             x, bp->height    , bp->stride, &v->s.gb);
            if(bp->height & 1) decode_rowskip(bp->data+x, bp->width - x, bp->height & 1, bp->stride, &v->s.gb);
        }
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
 * @param v VC-1 Context
 */
static int vop_dquant_decoding(VC1Context *v)
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


/** Do inverse transform
 */
static void vc1_inv_trans(DCTELEM block[64], int M, int N)
{
    int i;
    register int t1,t2,t3,t4,t5,t6,t7,t8;
    DCTELEM *src, *dst;

    src = block;
    dst = block;
    if(M==4){
        for(i = 0; i < N; i++){
            t1 = 17 * (src[0] + src[2]);
            t2 = 17 * (src[0] - src[2]);
            t3 = 22 * src[1];
            t4 = 22 * src[3];
            t5 = 10 * src[1];
            t6 = 10 * src[3];

            dst[0] = (t1 + t3 + t6 + 4) >> 3;
            dst[1] = (t2 - t4 + t5 + 4) >> 3;
            dst[2] = (t2 + t4 - t5 + 4) >> 3;
            dst[3] = (t1 - t3 - t6 + 4) >> 3;

            src += 8;
            dst += 8;
        }
    }else{
        for(i = 0; i < N; i++){
            t1 = 12 * (src[0] + src[4]);
            t2 = 12 * (src[0] - src[4]);
            t3 = 16 * src[2] +  6 * src[6];
            t4 =  6 * src[2] - 16 * src[6];

            t5 = t1 + t3;
            t6 = t2 + t4;
            t7 = t2 - t4;
            t8 = t1 - t3;

            t1 = 16 * src[1] + 15 * src[3] +  9 * src[5] +  4 * src[7];
            t2 = 15 * src[1] -  4 * src[3] - 16 * src[5] -  9 * src[7];
            t3 =  9 * src[1] - 16 * src[3] +  4 * src[5] + 15 * src[7];
            t4 =  4 * src[1] -  9 * src[3] + 15 * src[5] - 16 * src[7];

            dst[0] = (t5 + t1 + 4) >> 3;
            dst[1] = (t6 + t2 + 4) >> 3;
            dst[2] = (t7 + t3 + 4) >> 3;
            dst[3] = (t8 + t4 + 4) >> 3;
            dst[4] = (t8 - t4 + 4) >> 3;
            dst[5] = (t7 - t3 + 4) >> 3;
            dst[6] = (t6 - t2 + 4) >> 3;
            dst[7] = (t5 - t1 + 4) >> 3;

            src += 8;
            dst += 8;
        }
    }

    src = block;
    dst = block;
    if(N==4){
        for(i = 0; i < M; i++){
            t1 = 17 * (src[ 0] + src[16]);
            t2 = 17 * (src[ 0] - src[16]);
            t3 = 22 * src[ 8];
            t4 = 22 * src[24];
            t5 = 10 * src[ 8];
            t6 = 10 * src[24];

            dst[ 0] = (t1 + t3 + t6 + 64) >> 7;
            dst[ 8] = (t2 - t4 + t5 + 64) >> 7;
            dst[16] = (t2 + t4 - t5 + 64) >> 7;
            dst[24] = (t1 - t3 - t6 + 64) >> 7;

            src ++;
            dst ++;
        }
    }else{
        for(i = 0; i < M; i++){
            t1 = 12 * (src[ 0] + src[32]);
            t2 = 12 * (src[ 0] - src[32]);
            t3 = 16 * src[16] +  6 * src[48];
            t4 =  6 * src[16] - 16 * src[48];

            t5 = t1 + t3;
            t6 = t2 + t4;
            t7 = t2 - t4;
            t8 = t1 - t3;

            t1 = 16 * src[ 8] + 15 * src[24] +  9 * src[40] +  4 * src[56];
            t2 = 15 * src[ 8] -  4 * src[24] - 16 * src[40] -  9 * src[56];
            t3 =  9 * src[ 8] - 16 * src[24] +  4 * src[40] + 15 * src[56];
            t4 =  4 * src[ 8] -  9 * src[24] + 15 * src[40] - 16 * src[56];

            dst[ 0] = (t5 + t1 + 64) >> 7;
            dst[ 8] = (t6 + t2 + 64) >> 7;
            dst[16] = (t7 + t3 + 64) >> 7;
            dst[24] = (t8 + t4 + 64) >> 7;
            dst[32] = (t8 - t4 + 64 + 1) >> 7;
            dst[40] = (t7 - t3 + 64 + 1) >> 7;
            dst[48] = (t6 - t2 + 64 + 1) >> 7;
            dst[56] = (t5 - t1 + 64 + 1) >> 7;

            src++;
            dst++;
        }
    }
}

/** Apply overlap transform
 * @todo optimize
 * @todo move to DSPContext
 */
static void vc1_overlap_block(MpegEncContext *s, DCTELEM block[64], int n, int do_hor, int do_vert)
{
    int i;

    if(do_hor) { //TODO
    }
    if(do_vert) { //TODO
    }

    for(i = 0; i < 64; i++)
        block[i] += 128;
}


/** Put block onto picture
 * @todo move to DSPContext
 */
static void vc1_put_block(VC1Context *v, DCTELEM block[6][64])
{
    uint8_t *Y;
    int ys, us, vs;
    DSPContext *dsp = &v->s.dsp;

    ys = v->s.current_picture.linesize[0];
    us = v->s.current_picture.linesize[1];
    vs = v->s.current_picture.linesize[2];
    Y = v->s.dest[0];

    dsp->put_pixels_clamped(block[0], Y, ys);
    dsp->put_pixels_clamped(block[1], Y + 8, ys);
    Y += ys * 8;
    dsp->put_pixels_clamped(block[2], Y, ys);
    dsp->put_pixels_clamped(block[3], Y + 8, ys);

    dsp->put_pixels_clamped(block[4], v->s.dest[1], us);
    dsp->put_pixels_clamped(block[5], v->s.dest[2], vs);
}

/** Do motion compensation over 1 macroblock
 * Mostly adapted hpel_motion and qpel_motion from mpegvideo.c
 */
static void vc1_mc_1mv(VC1Context *v)
{
    MpegEncContext *s = &v->s;
    DSPContext *dsp = &v->s.dsp;
    uint8_t *srcY, *srcU, *srcV;
    int dxy, mx, my, src_x, src_y;
    int width = s->mb_width * 16, height = s->mb_height * 16;

    if(!v->s.last_picture.data[0])return;

    mx = s->mv[0][0][0] >> s->mspel;
    my = s->mv[0][0][1] >> s->mspel;
    srcY = s->last_picture.data[0];
    srcU = s->last_picture.data[1];
    srcV = s->last_picture.data[2];

    if(s->mspel) { // hpel mc
        dxy = ((my & 1) << 1) | (mx & 1);
        src_x = s->mb_x * 16 + (mx >> 1);
        src_y = s->mb_y * 16 + (my >> 1);
/*        src_x = clip(src_x, -16, width); //FIXME unneeded for emu?
        if (src_x == width)
            dxy &= ~1;
        src_y = clip(src_y, -16, height);
        if (src_y == height)
            dxy &= ~2;*/
        srcY += src_y * s->linesize + src_x;
        srcU += (src_y >> 1) * s->uvlinesize + (src_x >> 1);
        srcV += (src_y >> 1) * s->uvlinesize + (src_x >> 1);

        if((unsigned)src_x > s->h_edge_pos - (mx&1) - 16
           || (unsigned)src_y > s->v_edge_pos - (my&1) - 16){
            uint8_t *uvbuf= s->edge_emu_buffer + 18 * s->linesize;

            ff_emulated_edge_mc(s->edge_emu_buffer, srcY, s->linesize, 16+1, 16+1,
                             src_x, src_y, s->h_edge_pos, s->v_edge_pos);
            srcY = s->edge_emu_buffer;
            ff_emulated_edge_mc(uvbuf, srcU, s->uvlinesize, 8+1, 8+1,
                             src_x >> 1, src_y >> 1, s->h_edge_pos >> 1, s->v_edge_pos >> 1);
            ff_emulated_edge_mc(uvbuf + 16, srcV, s->uvlinesize, 8+1, 8+1,
                             src_x >> 1, src_y >> 1, s->h_edge_pos >> 1, s->v_edge_pos >> 1);
            srcU = uvbuf;
            srcV = uvbuf + 16;
        }
        dsp->put_no_rnd_pixels_tab[0][dxy](s->dest[0], srcY, s->linesize, 16);
        dsp->put_no_rnd_pixels_tab[1][0](s->dest[1], srcU, s->uvlinesize, 8);
        dsp->put_no_rnd_pixels_tab[1][0](s->dest[2], srcV, s->uvlinesize, 8);
    } else {
        int motion_x = mx, motion_y = my, uvdxy, uvsrc_x, uvsrc_y;
        dxy = ((motion_y & 3) << 2) | (motion_x & 3);
        src_x = s->mb_x * 16 + (mx >> 2);
        src_y = s->mb_y * 16 + (my >> 2);

        mx= motion_x/2;
        my= motion_y/2;

        mx= (mx>>1)|(mx&1);
        my= (my>>1)|(my&1);

        uvdxy= (mx&1) | ((my&1)<<1);
        mx>>=1;
        my>>=1;

        uvsrc_x = s->mb_x * 8 + mx;
        uvsrc_y = s->mb_y * 8 + my;

        srcY = s->last_picture.data[0] +   src_y *   s->linesize +   src_x;
        srcU = s->last_picture.data[1] + uvsrc_y * s->uvlinesize + uvsrc_x;
        srcV = s->last_picture.data[2] + uvsrc_y * s->uvlinesize + uvsrc_x;

        if(   (unsigned)src_x > s->h_edge_pos - (motion_x&3) - 16
              || (unsigned)src_y > s->v_edge_pos - (motion_y&3) - 16  ){
            uint8_t *uvbuf= s->edge_emu_buffer + 18*s->linesize;
            ff_emulated_edge_mc(s->edge_emu_buffer, srcY, s->linesize, 17, 17,
                                src_x, src_y, s->h_edge_pos, s->v_edge_pos);
            srcY = s->edge_emu_buffer;
            ff_emulated_edge_mc(uvbuf, srcU, s->uvlinesize, 9, 9,
                                    uvsrc_x, uvsrc_y, s->h_edge_pos>>1, s->v_edge_pos>>1);
            ff_emulated_edge_mc(uvbuf + 16, srcV, s->uvlinesize, 9, 9,
                                    uvsrc_x, uvsrc_y, s->h_edge_pos>>1, s->v_edge_pos>>1);
            srcU = uvbuf;
            srcV = uvbuf + 16;
        }

        dsp->put_no_rnd_qpel_pixels_tab[0][dxy](s->dest[0], srcY, s->linesize);
        dsp->put_no_rnd_pixels_tab[1][uvdxy](s->dest[1], srcU, s->uvlinesize, 8);
        dsp->put_no_rnd_pixels_tab[1][uvdxy](s->dest[2], srcV, s->uvlinesize, 8);
    }
}

/**
 * Decode Simple/Main Profiles sequence header
 * @see Figure 7-8, p16-17
 * @param avctx Codec context
 * @param gb GetBit context initialized from Codec context extra_data
 * @return Status
 */
static int decode_sequence_header(AVCodecContext *avctx, GetBitContext *gb)
{
    VC1Context *v = avctx->priv_data;

    av_log(avctx, AV_LOG_INFO, "Header: %0X\n", show_bits(gb, 32));
    v->profile = get_bits(gb, 2);
    if (v->profile == 2)
    {
        av_log(avctx, AV_LOG_ERROR, "Profile value 2 is forbidden (and WMV3 Complex Profile is unsupported)\n");
        return -1;
    }

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

    if (v->profile < PROFILE_ADVANCED)
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

    if (v->profile < PROFILE_ADVANCED)
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

    if (v->profile < PROFILE_ADVANCED)
    {
        v->s.resync_marker = get_bits(gb, 1);
        v->rangered = get_bits(gb, 1);
        if (v->rangered && v->profile == PROFILE_SIMPLE)
        {
            av_log(avctx, AV_LOG_INFO,
                   "RANGERED should be set to 0 in simple profile\n");
        }
    }

    v->s.max_b_frames = avctx->max_b_frames = get_bits(gb, 3); //common
    v->quantizer_mode = get_bits(gb, 2); //common

    if (v->profile < PROFILE_ADVANCED)
    {
        v->finterpflag = get_bits(gb, 1); //common
        v->res_rtm_flag = get_bits(gb, 1); //reserved
        if (!v->res_rtm_flag)
        {
            av_log(avctx, AV_LOG_ERROR,
                   "0 for reserved RES_RTM_FLAG is forbidden\n");
            //return -1;
        }
        av_log(avctx, AV_LOG_DEBUG,
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
    }
    return -1;
}


static int vc1_parse_frame_header(VC1Context *v, GetBitContext* gb)
{
    int pqindex, lowquant, status;

    if(v->finterpflag) v->interpfrm = get_bits(gb, 1);
    skip_bits(gb, 2); //framecnt unused
    v->rangeredfrm = 0;
    if (v->rangered) v->rangeredfrm = get_bits(gb, 1);
    v->s.pict_type = get_bits(gb, 1);
    if (v->s.avctx->max_b_frames) {
        if (!v->s.pict_type) {
            if (get_bits(gb, 1)) v->s.pict_type = I_TYPE;
            else v->s.pict_type = B_TYPE;
        } else v->s.pict_type = P_TYPE;
    } else v->s.pict_type = v->s.pict_type ? P_TYPE : I_TYPE;

    if(v->s.pict_type == I_TYPE)
        get_bits(gb, 7); // skip buffer fullness

    /* Quantizer stuff */
    pqindex = get_bits(gb, 5);
    if (v->quantizer_mode == QUANT_FRAME_IMPLICIT)
        v->pq = pquant_table[0][pqindex];
    else
        v->pq = pquant_table[v->quantizer_mode-1][pqindex];

    if (v->quantizer_mode == QUANT_FRAME_IMPLICIT)
        v->pquantizer = pqindex < 9;
    if (v->quantizer_mode == QUANT_UNIFORM || v->quantizer_mode == QUANT_NON_UNIFORM)
        v->pquantizer = v->quantizer_mode == QUANT_UNIFORM;
    v->pqindex = pqindex;
    if (pqindex < 9) v->halfpq = get_bits(gb, 1);
    else v->halfpq = 0;
    if (v->quantizer_mode == QUANT_FRAME_EXPLICIT)
        v->pquantizer = get_bits(gb, 1);
    v->dquantfrm = 0;

//av_log(v->s.avctx, AV_LOG_INFO, "%c Frame: QP=[%i]%i (+%i/2) %i\n",
//        (v->s.pict_type == P_TYPE) ? 'P' : ((v->s.pict_type == I_TYPE) ? 'I' : 'B'), pqindex, v->pq, v->halfpq, v->rangeredfrm);

    //TODO: complete parsing for P/B/BI frames
    switch(v->s.pict_type) {
    case P_TYPE:
        if (v->pq < 5) v->tt_index = 0;
        else if(v->pq < 13) v->tt_index = 1;
        else v->tt_index = 2;

        if (v->extended_mv == 1) v->mvrange = get_prefix(gb, 0, 3);
        v->k_x = v->mvrange + 9 + (v->mvrange >> 1); //k_x can be 9 10 12 13
        v->k_y = v->mvrange + 8; //k_y can be 8 9 10 11
        v->range_x = 1 << (v->k_x - 1);
        v->range_y = 1 << (v->k_y - 1);
        if (v->profile == PROFILE_ADVANCED)
        {
            if (v->postprocflag) v->postproc = get_bits(gb, 1);
        }
        else
            if (v->multires) v->respic = get_bits(gb, 2);
        lowquant = (v->pq > 12) ? 0 : 1;
        v->mv_mode = mv_pmode_table[lowquant][get_prefix(gb, 1, 4)];
        if (v->mv_mode == MV_PMODE_INTENSITY_COMP)
        {
            v->mv_mode2 = mv_pmode_table[lowquant][get_prefix(gb, 1, 3)];
            v->lumscale = get_bits(gb, 6);
            v->lumshift = get_bits(gb, 6);
        }
        if(v->mv_mode == MV_PMODE_1MV_HPEL || v->mv_mode == MV_PMODE_1MV_HPEL_BILIN)
            v->s.mspel = 1;
        else
            v->s.mspel = 0;

if(v->mv_mode != MV_PMODE_1MV && v->mv_mode != MV_PMODE_1MV_HPEL && v->mv_mode != MV_PMODE_1MV_HPEL_BILIN) {
    av_log(v->s.avctx, AV_LOG_ERROR, "Only 1MV P-frames are supported by now\n");
    return -1;
}
        if ((v->mv_mode == MV_PMODE_INTENSITY_COMP &&
                 v->mv_mode2 == MV_PMODE_MIXED_MV)
                || v->mv_mode == MV_PMODE_MIXED_MV)
        {
            status = bitplane_decoding(&v->mv_type_mb_plane, v);
            if (status < 0) return -1;
            av_log(v->s.avctx, AV_LOG_DEBUG, "MB MV Type plane encoding: "
                   "Imode: %i, Invert: %i\n", status>>1, status&1);
        }
        status = bitplane_decoding(&v->skip_mb_plane, v);
        if (status < 0) return -1;
        av_log(v->s.avctx, AV_LOG_DEBUG, "MB Skip plane encoding: "
               "Imode: %i, Invert: %i\n", status>>1, status&1);

        /* Hopefully this is correct for P frames */
        v->s.mv_table_index = get_bits(gb, 2); //but using vc1_ tables
        v->cbpcy_vlc = &vc1_cbpcy_p_vlc[get_bits(gb, 2)];

        if (v->dquant)
        {
            av_log(v->s.avctx, AV_LOG_DEBUG, "VOP DQuant info\n");
            vop_dquant_decoding(v);
        }

        v->ttfrm = 0; //FIXME Is that so ?
        if (v->vstransform)
        {
            v->ttmbf = get_bits(gb, 1);
            if (v->ttmbf)
            {
                v->ttfrm = get_bits(gb, 2);
            }
        }
        break;
    case B_TYPE:
        break;
    }

    /* AC Syntax */
    v->c_ac_table_index = decode012(gb);
    if (v->s.pict_type == I_TYPE || v->s.pict_type == BI_TYPE)
    {
        v->y_ac_table_index = decode012(gb);
    }
    /* DC Syntax */
    v->s.dc_table_index = get_bits(gb, 1);

    return 0;
}

/***********************************************************************/
/**
 * @defgroup block VC-1 Block-level functions
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
  index = 1 + get_vlc2(gb, vc1_mv_diff_vlc[s->mv_table_index].table,\
                       VC1_MV_DIFF_VLC_BITS, 2);                    \
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
    _dmv_x = get_bits(gb, v->k_x - s->mspel);                       \
    _dmv_y = get_bits(gb, v->k_y - s->mspel);                       \
  }                                                                 \
  else if (index == 36)                                             \
  {                                                                 \
    _dmv_x = 0;                                                     \
    _dmv_y = 0;                                                     \
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

/** Predict and set motion vector
 */
static inline void vc1_pred_mv(MpegEncContext *s, int dmv_x, int dmv_y, int mv1, int r_x, int r_y)
{
    int xy, wrap, off;
    int16_t *A, *B, *C;
    int px, py;
    int sum;
    int mb_pos = s->mb_x + s->mb_y * s->mb_stride;

    /* scale MV difference to be quad-pel */
    dmv_x <<= s->mspel;
    dmv_y <<= s->mspel;

    wrap = s->b8_stride;
    xy = s->block_index[0];

    C = s->current_picture.motion_val[0][xy - (1 << mv1)];
    A = s->current_picture.motion_val[0][xy - (wrap << mv1)];
    off = (s->mb_x == (s->mb_width - 1)) ? -1 : 1;
    B = s->current_picture.motion_val[0][xy + ((off - wrap) << mv1)];

    if(!s->first_slice_line) { // predictor A is not out of bounds
        if(s->mb_width == 1) {
            px = A[0];
            py = A[1];
        } else {
            px = mid_pred(A[0], B[0], C[0]);
            py = mid_pred(A[1], B[1], C[1]);
        }
    } else if(s->mb_x) { // predictor C is not out of bounds
        px = C[0];
        py = C[1];
    } else {
        px = py = 0;
    }
    if(s->mb_intra) px = py = 0;

    /* Pullback MV as specified in 8.3.5.3.4 */
    {
        int qx, qy, X, Y;
        qx = s->mb_x << 6; //FIXME: add real block coords for 4MV mode
        qy = s->mb_y << 6;
        X = (s->mb_width << 6) - 4;
        Y = (s->mb_height << 6) - 4;
        if(mv1) {
            if(qx + px < -60) px = -60 - qx;
            if(qy + py < -60) py = -60 - qy;
        } else {
            if(qx + px < -28) px = -28 - qx;
            if(qy + py < -28) py = -28 - qy;
        }
        if(qx + px > X) px = X - qx;
        if(qy + py > Y) py = Y - qy;
    }
    /* Calculate hybrid prediction as specified in 8.3.5.3.5 */
    if(!s->mb_intra && !s->first_slice_line && s->mb_x) {
        if(IS_INTRA(s->current_picture.mb_type[mb_pos - s->mb_stride]))
            sum = ABS(px) + ABS(py);
        else
            sum = ABS(px - A[0]) + ABS(py - A[1]);
        if(sum > 32) {
            if(get_bits1(&s->gb)) {
                px = A[0];
                py = A[1];
            } else {
                px = C[0];
                py = C[1];
            }
        } else {
            if(IS_INTRA(s->current_picture.mb_type[mb_pos - 1]))
                sum = ABS(px) + ABS(py);
            else
                sum = ABS(px - C[0]) + ABS(py - C[1]);
            if(sum > 32) {
                if(get_bits1(&s->gb)) {
                    px = A[0];
                    py = A[1];
                } else {
                    px = C[0];
                    py = C[1];
                }
            }
        }
    }
    /* store MV using signed modulus of MV range defined in 4.11 */
    s->mv[0][0][0] = s->current_picture.motion_val[0][xy][0] = ((px + dmv_x + r_x) & ((r_x << 1) - 1)) - r_x;
    s->mv[0][0][1] = s->current_picture.motion_val[0][xy][1] = ((py + dmv_y + r_y) & ((r_y << 1) - 1)) - r_y;
}

/** Get predicted DC value for I-frames only
 * prediction dir: left=0, top=1
 * @param s MpegEncContext
 * @param[in] n block index in the current MB
 * @param dc_val_ptr Pointer to DC predictor
 * @param dir_ptr Prediction direction for use in AC prediction
 */
static inline int vc1_i_pred_dc(MpegEncContext *s, int overlap, int pq, int n,
                              int16_t **dc_val_ptr, int *dir_ptr)
{
    int a, b, c, wrap, pred, scale;
    int16_t *dc_val;
    static const uint16_t dcpred[32] = {
    -1, 1024,  512,  341,  256,  205,  171,  146,  128,
         114,  102,   93,   85,   79,   73,   68,   64,
          60,   57,   54,   51,   49,   47,   45,   43,
          41,   39,   38,   37,   35,   34,   33
    };

    /* find prediction - wmv3_dc_scale always used here in fact */
    if (n < 4)     scale = s->y_dc_scale;
    else           scale = s->c_dc_scale;

    wrap = s->block_wrap[n];
    dc_val= s->dc_val[0] + s->block_index[n];

    /* B A
     * C X
     */
    c = dc_val[ - 1];
    b = dc_val[ - 1 - wrap];
    a = dc_val[ - wrap];

    if (pq < 9 || !overlap)
    {
        /* Set outer values */
        if (!s->mb_y && (n!=2 && n!=3)) b=a=dcpred[scale];
        if (s->mb_x == 0 && (n!=1 && n!=3)) b=c=dcpred[scale];
    }
    else
    {
        /* Set outer values */
        if (!s->mb_y && (n!=2 && n!=3)) b=a=0;
        if (s->mb_x == 0 && (n!=1 && n!=3)) b=c=0;
    }

    if (abs(a - b) <= abs(b - c)) {
        pred = c;
        *dir_ptr = 1;//left
    } else {
        pred = a;
        *dir_ptr = 0;//top
    }

    /* update predictor */
    *dc_val_ptr = &dc_val[0];
    return pred;
}


/** Get predicted DC value
 * prediction dir: left=0, top=1
 * @param s MpegEncContext
 * @param[in] n block index in the current MB
 * @param dc_val_ptr Pointer to DC predictor
 * @param dir_ptr Prediction direction for use in AC prediction
 */
static inline int vc1_pred_dc(MpegEncContext *s, int overlap, int pq, int n,
                              int a_avail, int c_avail,
                              int16_t **dc_val_ptr, int *dir_ptr)
{
    int a, b, c, wrap, pred, scale;
    int16_t *dc_val;
    int mb_pos = s->mb_x + s->mb_y * s->mb_stride;
    int mb_pos2, q1, q2;

    /* find prediction - wmv3_dc_scale always used here in fact */
    if (n < 4)     scale = s->y_dc_scale;
    else           scale = s->c_dc_scale;

    wrap = s->block_wrap[n];
    dc_val= s->dc_val[0] + s->block_index[n];

    /* B A
     * C X
     */
    c = dc_val[ - 1];
    b = dc_val[ - 1 - wrap];
    a = dc_val[ - wrap];

    if(a_avail && c_avail) {
        if(abs(a - b) <= abs(b - c)) {
            pred = c;
            *dir_ptr = 1;//left
        } else {
            pred = a;
            *dir_ptr = 0;//top
        }
    } else if(a_avail) {
        pred = a;
        *dir_ptr = 0;//top
    } else if(c_avail) {
        pred = c;
        *dir_ptr = 1;//left
    } else {
        pred = 0;
        *dir_ptr = 1;//left
    }

    /* scale coeffs if needed */
    mb_pos2 = mb_pos - *dir_ptr - (1 - *dir_ptr) * s->mb_stride;
    q1 = s->current_picture.qscale_table[mb_pos];
    q2 = s->current_picture.qscale_table[mb_pos2];
    if(0 && q1 && q2 && q1 != q2) {
        q1 = s->y_dc_scale_table[q1];
        q2 = s->y_dc_scale_table[q2];
        pred = (pred * q2 * vc1_dqscale[q1 - 1] + 0x20000) >> 18;
    }

    /* update predictor */
    *dc_val_ptr = &dc_val[0];
    return pred;
}


/**
 * @defgroup std_mb VC1 Macroblock-level functions in Simple/Main Profiles
 * @see 7.1.4, p91 and 8.1.1.7, p(1)04
 * @todo TODO: Integrate to MpegEncContext facilities
 * @{
 */

static inline int vc1_coded_block_pred(MpegEncContext * s, int n, uint8_t **coded_block_ptr)
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

/**
 * Decode one AC coefficient
 * @param v The VC1 context
 * @param last Last coefficient
 * @param skip How much zero coefficients to skip
 * @param value Decoded AC coefficient value
 * @see 8.1.3.4
 */
static void vc1_decode_ac_coeff(VC1Context *v, int *last, int *skip, int *value, int codingset)
{
    GetBitContext *gb = &v->s.gb;
    int index, escape, run = 0, level = 0, lst = 0;

    index = get_vlc2(gb, vc1_ac_coeff_table[codingset].table, AC_VLC_BITS, 3);
    if (index != vc1_ac_sizes[codingset] - 1) {
        run = vc1_index_decode_table[codingset][index][0];
        level = vc1_index_decode_table[codingset][index][1];
        lst = index >= vc1_last_decode_table[codingset];
        if(get_bits(gb, 1))
            level = -level;
    } else {
        escape = decode210(gb);
        if (escape == 0) {
            index = get_vlc2(gb, vc1_ac_coeff_table[codingset].table, AC_VLC_BITS, 3);
            run = vc1_index_decode_table[codingset][index][0];
            level = vc1_index_decode_table[codingset][index][1];
            lst = index >= vc1_last_decode_table[codingset];
            if(lst)
                level += vc1_last_delta_level_table[codingset][run];
            else
                level += vc1_delta_level_table[codingset][run];
            if(get_bits(gb, 1))
                level = -level;
        } else if (escape == 1) {
            index = get_vlc2(gb, vc1_ac_coeff_table[codingset].table, AC_VLC_BITS, 3);
            run = vc1_index_decode_table[codingset][index][0];
            level = vc1_index_decode_table[codingset][index][1];
            lst = index >= vc1_last_decode_table[codingset];
            if(lst)
                run += vc1_last_delta_run_table[codingset][level] + 1;
            else
                run += vc1_delta_run_table[codingset][level] + 1;
            if(get_bits(gb, 1))
                level = -level;
        } else {
            int sign;
            lst = get_bits(gb, 1);
            if(v->s.esc3_level_length == 0) {
                if(v->pq < 8 || v->dquantfrm) { // table 59
                    v->s.esc3_level_length = get_bits(gb, 3);
                    if(!v->s.esc3_level_length)
                        v->s.esc3_level_length = get_bits(gb, 2) + 8;
                } else { //table 60
                    v->s.esc3_level_length = get_prefix(gb, 1, 6) + 2;
                }
                v->s.esc3_run_length = 3 + get_bits(gb, 2);
            }
            run = get_bits(gb, v->s.esc3_run_length);
            sign = get_bits(gb, 1);
            level = get_bits(gb, v->s.esc3_level_length);
            if(sign)
                level = -level;
        }
    }

    *last = lst;
    *skip = run;
    *value = level;
}

/** Decode intra block in intra frames - should be faster than decode_intra_block
 * @param v VC1Context
 * @param block block to decode
 * @param coded are AC coeffs present or not
 * @param codingset set of VLC to decode data
 */
static int vc1_decode_i_block(VC1Context *v, DCTELEM block[64], int n, int coded, int codingset)
{
    GetBitContext *gb = &v->s.gb;
    MpegEncContext *s = &v->s;
    int dc_pred_dir = 0; /* Direction of the DC prediction used */
    int run_diff, i;
    int16_t *dc_val;
    int16_t *ac_val, *ac_val2;
    int dcdiff;

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
            if (v->pq == 1) dcdiff = get_bits(gb, 10);
            else if (v->pq == 2) dcdiff = get_bits(gb, 9);
            else dcdiff = get_bits(gb, 8);
        }
        else
        {
            if (v->pq == 1)
                dcdiff = (dcdiff<<2) + get_bits(gb, 2) - 3;
            else if (v->pq == 2)
                dcdiff = (dcdiff<<1) + get_bits(gb, 1) - 1;
        }
        if (get_bits(gb, 1))
            dcdiff = -dcdiff;
    }

    /* Prediction */
    dcdiff += vc1_i_pred_dc(&v->s, v->overlap, v->pq, n, &dc_val, &dc_pred_dir);
    *dc_val = dcdiff;

    /* Store the quantized DC coeff, used for prediction */

    if (n < 4) {
        block[0] = dcdiff * s->y_dc_scale;
    } else {
        block[0] = dcdiff * s->c_dc_scale;
    }
    /* Skip ? */
    run_diff = 0;
    i = 0;
    if (!coded) {
        goto not_coded;
    }

    //AC Decoding
    i = 1;

    {
        int last = 0, skip, value;
        const int8_t *zz_table;
        int scale;
        int k;

        scale = v->pq * 2 + v->halfpq;

        if(v->s.ac_pred) {
            if(!dc_pred_dir)
                zz_table = vc1_horizontal_zz;
            else
                zz_table = vc1_vertical_zz;
        } else
            zz_table = vc1_normal_zz;

        ac_val = s->ac_val[0][0] + s->block_index[n] * 16;
        ac_val2 = ac_val;
        if(dc_pred_dir) //left
            ac_val -= 16;
        else //top
            ac_val -= 16 * s->block_wrap[n];

        while (!last) {
            vc1_decode_ac_coeff(v, &last, &skip, &value, codingset);
            i += skip;
            if(i > 63)
                break;
            block[zz_table[i++]] = value;
        }

        /* apply AC prediction if needed */
        if(s->ac_pred) {
            if(dc_pred_dir) { //left
                for(k = 1; k < 8; k++)
                    block[k << 3] += ac_val[k];
            } else { //top
                for(k = 1; k < 8; k++)
                    block[k] += ac_val[k + 8];
            }
        }
        /* save AC coeffs for further prediction */
        for(k = 1; k < 8; k++) {
            ac_val2[k] = block[k << 3];
            ac_val2[k + 8] = block[k];
        }

        /* scale AC coeffs */
        for(k = 1; k < 64; k++)
            if(block[k]) {
                block[k] *= scale;
                if(!v->pquantizer)
                    block[k] += (block[k] < 0) ? -v->pq : v->pq;
            }

        if(s->ac_pred) i = 63;
    }

not_coded:
    if(!coded) {
        int k, scale;
        ac_val = s->ac_val[0][0] + s->block_index[n] * 16;
        ac_val2 = ac_val;

        scale = v->pq * 2 + v->halfpq;
        memset(ac_val2, 0, 16 * 2);
        if(dc_pred_dir) {//left
            ac_val -= 16;
            if(s->ac_pred)
                memcpy(ac_val2, ac_val, 8 * 2);
        } else {//top
            ac_val -= 16 * s->block_wrap[n];
            if(s->ac_pred)
                memcpy(ac_val2 + 8, ac_val + 8, 8 * 2);
        }

        /* apply AC prediction if needed */
        if(s->ac_pred) {
            if(dc_pred_dir) { //left
                for(k = 1; k < 8; k++) {
                    block[k << 3] = ac_val[k] * scale;
                    if(!v->pquantizer)
                        block[k << 3] += (block[k << 3] < 0) ? -v->pq : v->pq;
                }
            } else { //top
                for(k = 1; k < 8; k++) {
                    block[k] = ac_val[k + 8] * scale;
                    if(!v->pquantizer)
                        block[k] += (block[k] < 0) ? -v->pq : v->pq;
                }
            }
            i = 63;
        }
    }
    s->block_last_index[n] = i;

    return 0;
}

/** Decode intra block in inter frames - more generic version than vc1_decode_i_block
 * @param v VC1Context
 * @param block block to decode
 * @param coded are AC coeffs present or not
 * @param mquant block quantizer
 * @param codingset set of VLC to decode data
 */
static int vc1_decode_intra_block(VC1Context *v, DCTELEM block[64], int n, int coded, int mquant, int codingset)
{
    GetBitContext *gb = &v->s.gb;
    MpegEncContext *s = &v->s;
    int dc_pred_dir = 0; /* Direction of the DC prediction used */
    int run_diff, i;
    int16_t *dc_val;
    int16_t *ac_val, *ac_val2;
    int dcdiff;
    int mb_pos = s->mb_x + s->mb_y * s->mb_stride;
    int a_avail = v->a_avail, c_avail = v->c_avail;

    /* XXX: Guard against dumb values of mquant */
    mquant = (mquant < 1) ? 0 : ( (mquant>31) ? 31 : mquant );

    /* Set DC scale - y and c use the same */
    s->y_dc_scale = s->y_dc_scale_table[mquant];
    s->c_dc_scale = s->c_dc_scale_table[mquant];

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
    dcdiff += vc1_pred_dc(&v->s, v->overlap, mquant, n, a_avail, c_avail, &dc_val, &dc_pred_dir);
    *dc_val = dcdiff;

    /* Store the quantized DC coeff, used for prediction */

    if (n < 4) {
        block[0] = dcdiff * s->y_dc_scale;
    } else {
        block[0] = dcdiff * s->c_dc_scale;
    }
    /* Skip ? */
    run_diff = 0;
    i = 0;
    if (!coded) {
        goto not_coded;
    }

    //AC Decoding
    i = 1;

    {
        int last = 0, skip, value;
        const int8_t *zz_table;
        int scale;
        int k;

        scale = mquant * 2;

        zz_table = vc1_simple_progressive_8x8_zz;

        ac_val = s->ac_val[0][0] + s->block_index[n] * 16;
        ac_val2 = ac_val;
        if(dc_pred_dir) //left
            ac_val -= 16;
        else //top
            ac_val -= 16 * s->block_wrap[n];

        while (!last) {
            vc1_decode_ac_coeff(v, &last, &skip, &value, codingset);
            i += skip;
            if(i > 63)
                break;
            block[zz_table[i++]] = value;
        }

        /* apply AC prediction if needed */
        if(s->ac_pred) {
            /* scale predictors if needed*/
            int mb_pos2, q1, q2;

            mb_pos2 = mb_pos - dc_pred_dir - (1 - dc_pred_dir) * s->mb_stride;
            q1 = s->current_picture.qscale_table[mb_pos];
            q2 = s->current_picture.qscale_table[mb_pos2];

            if(!c_avail) {
                memset(ac_val, 0, 8 * sizeof(ac_val[0]));
                dc_pred_dir = 0;
            }
            if(!a_avail) {
                memset(ac_val + 8, 0, 8 * sizeof(ac_val[0]));
                dc_pred_dir = 1;
            }
            if(!q1 && q1 && q2 && q1 != q2) {
                q1 = q1 * 2 - 1;
                q2 = q2 * 2 - 1;

                if(dc_pred_dir) { //left
                    for(k = 1; k < 8; k++)
                        block[k << 3] += (ac_val[k] * q2 * vc1_dqscale[q1 - 1] + 0x20000) >> 18;
                } else { //top
                    for(k = 1; k < 8; k++)
                        block[k] += (ac_val[k + 8] * q2 * vc1_dqscale[q1 - 1] + 0x20000) >> 18;
                }
            } else {
                if(dc_pred_dir) { //left
                    for(k = 1; k < 8; k++)
                        block[k << 3] += ac_val[k];
                } else { //top
                    for(k = 1; k < 8; k++)
                        block[k] += ac_val[k + 8];
                }
            }
        }
        /* save AC coeffs for further prediction */
        for(k = 1; k < 8; k++) {
            ac_val2[k] = block[k << 3];
            ac_val2[k + 8] = block[k];
        }

        /* scale AC coeffs */
        for(k = 1; k < 64; k++)
            if(block[k]) {
                block[k] *= scale;
                if(!v->pquantizer)
                    block[k] += (block[k] < 0) ? -mquant : mquant;
            }

        if(s->ac_pred) i = 63;
    }

not_coded:
    if(!coded) {
        int k, scale;
        ac_val = s->ac_val[0][0] + s->block_index[n] * 16;
        ac_val2 = ac_val;

        if(!c_avail) {
            memset(ac_val, 0, 8 * sizeof(ac_val[0]));
            dc_pred_dir = 0;
        }
        if(!a_avail) {
            memset(ac_val + 8, 0, 8 * sizeof(ac_val[0]));
            dc_pred_dir = 1;
        }

        scale = mquant * 2;
        memset(ac_val2, 0, 16 * 2);
        if(dc_pred_dir) {//left
            ac_val -= 16;
            if(s->ac_pred)
                memcpy(ac_val2, ac_val, 8 * 2);
        } else {//top
            ac_val -= 16 * s->block_wrap[n];
            if(s->ac_pred)
                memcpy(ac_val2 + 8, ac_val + 8, 8 * 2);
        }

        /* apply AC prediction if needed */
        if(s->ac_pred) {
            if(dc_pred_dir) { //left
                for(k = 1; k < 8; k++) {
                    block[k << 3] = ac_val[k] * scale;
                    if(!v->pquantizer)
                        block[k << 3] += (block[k << 3] < 0) ? -mquant : mquant;
                }
            } else { //top
                for(k = 1; k < 8; k++) {
                    block[k] = ac_val[k + 8] * scale;
                    if(!v->pquantizer)
                        block[k] += (block[k] < 0) ? -mquant : mquant;
                }
            }
            i = 63;
        }
    }
    s->block_last_index[n] = i;

    return 0;
}

/** Decode P block
 */
static int vc1_decode_p_block(VC1Context *v, DCTELEM block[64], int n, int mquant, int ttmb, int first_block)
{
    MpegEncContext *s = &v->s;
    GetBitContext *gb = &s->gb;
    int i, j;
    int subblkpat = 0;
    int scale, off, idx, last, skip, value;
    int ttblk = ttmb & 7;

    if(ttmb == -1) {
        ttblk = ttblk_to_tt[v->tt_index][get_vlc2(gb, vc1_ttblk_vlc[v->tt_index].table, VC1_TTBLK_VLC_BITS, 1)];
    }
    if(ttblk == TT_4X4) {
        subblkpat = ~(get_vlc2(gb, vc1_subblkpat_vlc[v->tt_index].table, VC1_SUBBLKPAT_VLC_BITS, 1) + 1);
    }
    if((ttblk != TT_8X8 && ttblk != TT_4X4) && (v->ttmbf || (ttmb != -1 && (ttmb & 8) && !first_block))) {
        subblkpat = decode012(gb);
        if(subblkpat) subblkpat ^= 3; //swap decoded pattern bits
        if(ttblk == TT_8X4_TOP || ttblk == TT_8X4_BOTTOM) ttblk = TT_8X4;
        if(ttblk == TT_4X8_RIGHT || ttblk == TT_4X8_LEFT) ttblk = TT_4X8;
    }
    scale = 2 * mquant;

    // convert transforms like 8X4_TOP to generic TT and SUBBLKPAT
    if(ttblk == TT_8X4_TOP || ttblk == TT_8X4_BOTTOM) {
        ttblk = TT_8X4;
        subblkpat = 2 - (ttblk == TT_8X4_TOP);
    }
    if(ttblk == TT_4X8_RIGHT || ttblk == TT_4X8_LEFT) {
        ttblk = TT_4X8;
        subblkpat = 2 - (ttblk == TT_4X8_LEFT);
    }

    switch(ttblk) {
    case TT_8X8:
        i = 0;
        last = 0;
        while (!last) {
            vc1_decode_ac_coeff(v, &last, &skip, &value, v->codingset2);
            i += skip;
            if(i > 63)
                break;
            idx = vc1_simple_progressive_8x8_zz[i++];
            block[idx] = value * scale;
        }
        vc1_inv_trans(block, 8, 8);
        break;
    case TT_4X4:
        for(j = 0; j < 4; j++) {
            last = subblkpat & (1 << (3 - j));
            i = 0;
            off = (j & 1) * 4 + (j & 2) * 32;
            while (!last) {
                vc1_decode_ac_coeff(v, &last, &skip, &value, v->codingset2);
                i += skip;
                if(i > 15)
                    break;
                idx = vc1_simple_progressive_4x4_zz[i++];
                block[idx + off] = value * scale;
            }
            vc1_inv_trans(block + off, 4, 4);
        }
        break;
    case TT_8X4:
        for(j = 0; j < 2; j++) {
            last = subblkpat & (1 << (1 - j));
            i = 0;
            off = j * 32;
            while (!last) {
                vc1_decode_ac_coeff(v, &last, &skip, &value, v->codingset2);
                i += skip;
                if(i > 31)
                    break;
                idx = vc1_simple_progressive_8x4_zz[i++];
                block[idx + off] = value * scale;
            }
            if(!(subblkpat & (1 << (1 - j)))) vc1_inv_trans(block + off, 8, 4);
        }
        break;
    case TT_4X8:
        for(j = 0; j < 2; j++) {
            last = subblkpat & (1 << (1 - j));
            i = 0;
            off = j * 4;
            while (!last) {
                vc1_decode_ac_coeff(v, &last, &skip, &value, v->codingset2);
                i += skip;
                if(i > 31)
                    break;
                idx = vc1_simple_progressive_8x4_zz[i++];
                block[idx + off] = value * scale;
            }
            vc1_inv_trans(block + off, 4, 8);
        }
        break;
    }
    return 0;
}


/** Decode one P-frame MB (in Simple/Main profile)
 * @todo TODO: Extend to AP
 * @fixme FIXME: DC value for inter blocks not set
 */
static int vc1_decode_p_mb(VC1Context *v, DCTELEM block[6][64])
{
    MpegEncContext *s = &v->s;
    GetBitContext *gb = &s->gb;
    int i, j, mb_offset = s->mb_x + s->mb_y*s->mb_width; /* XXX: mb_stride */
    int mb_pos = s->mb_x + s->mb_y * s->mb_stride;
    int cbp; /* cbp decoding stuff */
    int hybrid_pred; /* Prediction types */
    int mqdiff, mquant; /* MB quantization */
    int ttmb = v->ttmb; /* MB Transform type */
    int status;

    static const int size_table[6] = { 0, 2, 3, 4, 5, 8 },
      offset_table[6] = { 0, 1, 3, 7, 15, 31 };
    int mb_has_coeffs = 1; /* last_flag */
    int dmv_x, dmv_y; /* Differential MV components */
    int index, index1; /* LUT indices */
    int val, sign; /* temp values */
    int first_block = 1;
    int dst_idx, off;

    mquant = v->pq; /* Loosy initialization */

    if (v->mv_type_mb_plane.is_raw)
        v->mv_type_mb_plane.data[mb_offset] = get_bits(gb, 1);
    if (v->skip_mb_plane.is_raw)
        v->skip_mb_plane.data[mb_offset] = get_bits(gb, 1);
    s->current_picture.mbskip_table[mb_pos] = v->skip_mb_plane.data[mb_offset];
    if (!v->mv_type_mb_plane.data[mb_offset]) /* 1MV mode */
    {
        if (!v->skip_mb_plane.data[mb_offset])
        {
            GET_MVDATA(dmv_x, dmv_y);

            s->current_picture.mb_type[mb_pos] = s->mb_intra ? MB_TYPE_INTRA : MB_TYPE_16x16;
            vc1_pred_mv(s, dmv_x, dmv_y, 1, v->range_x, v->range_y);

            /* FIXME Set DC val for inter block ? */
            if (s->mb_intra && !mb_has_coeffs)
            {
                GET_MQUANT();
                s->ac_pred = get_bits(gb, 1);
                cbp = 0;
            }
            else if (mb_has_coeffs)
            {
                if (s->mb_intra) s->ac_pred = get_bits(gb, 1);
                cbp = get_vlc2(&v->s.gb, v->cbpcy_vlc->table, VC1_CBPCY_P_VLC_BITS, 2);
                GET_MQUANT();
            }
            else
            {
                mquant = v->pq;
                cbp = 0;
            }
            s->current_picture.qscale_table[mb_pos] = mquant;

            if (!v->ttmbf && !s->mb_intra && mb_has_coeffs)
                ttmb = get_vlc2(gb, vc1_ttmb_vlc[v->tt_index].table,
                                VC1_TTMB_VLC_BITS, 2);
            s->dsp.clear_blocks(block[0]);
            vc1_mc_1mv(v);
            dst_idx = 0;
            for (i=0; i<6; i++)
            {
                s->dc_val[0][s->block_index[i]] = 0;
                dst_idx += i >> 2;
                val = ((cbp >> (5 - i)) & 1);
                off = (i & 4) ? 0 : ((i & 1) * 8 + (i & 2) * 4 * s->linesize);
                if(s->mb_intra) {
                    /* check if prediction blocks A and C are available */
                    v->a_avail = v->c_avail = 0;
                    if((i == 2 || i == 3) || (s->mb_y && IS_INTRA(s->current_picture.mb_type[mb_pos - s->mb_stride])))
                        v->a_avail = 1;
                    if((i == 1 || i == 3) || (s->mb_x && IS_INTRA(s->current_picture.mb_type[mb_pos - 1])))
                        v->c_avail = 1;

                    vc1_decode_intra_block(v, block[i], i, val, mquant, (i&4)?v->codingset2:v->codingset);
                    vc1_inv_trans(s->block[i], 8, 8);
                    for(j = 0; j < 64; j++) s->block[i][j] += 128;
                    s->dsp.put_pixels_clamped(s->block[i], s->dest[dst_idx] + off, s->linesize >> ((i & 4) >> 2));
                    /* TODO: proper loop filtering */
                    if(v->a_avail)
                        s->dsp.h263_v_loop_filter(s->dest[dst_idx] + off, s->linesize >> ((i & 4) >> 2), s->y_dc_scale);
                    if(v->c_avail)
                        s->dsp.h263_h_loop_filter(s->dest[dst_idx] + off, s->linesize >> ((i & 4) >> 2), s->y_dc_scale);
                } else if(val) {
                    vc1_decode_p_block(v, block[i], i, mquant, ttmb, first_block);
                    if(!v->ttmbf && ttmb < 8) ttmb = -1;
                    first_block = 0;
                    s->dsp.add_pixels_clamped(s->block[i], s->dest[dst_idx] + off, (i&4)?s->uvlinesize:s->linesize);
                }
            }
        }
        else //Skipped
        {
            s->mb_intra = 0;
            s->current_picture.mb_type[mb_pos] = MB_TYPE_SKIP;
            vc1_pred_mv(s, 0, 0, 1, v->range_x, v->range_y);
            vc1_mc_1mv(v);
            return 0;
        }
    } //1MV mode
    else //4MV mode
    {//FIXME: looks not conforming to standard and is not even theoretically complete
        if (!v->skip_mb_plane.data[mb_offset] /* unskipped MB */)
        {
            int blk_intra[4], blk_coded[4];
            /* Get CBPCY */
            cbp = get_vlc2(&v->s.gb, v->cbpcy_vlc->table, VC1_CBPCY_P_VLC_BITS, 2);
            for (i=0; i<4; i++)
            {
                val = ((cbp >> (5 - i)) & 1);
                blk_intra[i] = 0;
                blk_coded[i] = val;
                if(val) {
                    GET_MVDATA(dmv_x, dmv_y);
                    blk_intra[i] = s->mb_intra;
                }
                if (v->mv_mode == MV_PMODE_MIXED_MV /* Hybrid pred */)
                    hybrid_pred = get_bits(gb, 1);
            }
            if((blk_intra[0] | blk_intra[1] | blk_intra[2] | blk_intra[3]) ||
                (blk_coded[0] | blk_coded[1] | blk_coded[2] | blk_coded[3])) {
                GET_MQUANT();

                if (s->mb_intra /* One of the 4 blocks is intra */
                    /* non-zero pred for that block */)
                    s->ac_pred = get_bits(gb, 1);
                if (!v->ttmbf)
                    ttmb = get_vlc2(gb, vc1_ttmb_vlc[v->tt_index].table,
                                    VC1_TTMB_VLC_BITS, 12);
                for(i = 0; i < 6; i++) {
                    val = ((cbp >> (5 - i)) & 1);
                    if(i & 4 || blk_intra[i] || val) {
                        if(i < 4 && blk_intra[i])
                            status = vc1_decode_intra_block(v, block[i], i, val, mquant, (i&4)?v->codingset2:v->codingset);
                        else
                            status = vc1_decode_p_block(v, block[i], i, mquant, ttmb, 0);
                    }
                }
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
            }
            /* TODO: blah */
            return 0;
        }
    }

    /* Should never happen */
    return -1;
}

/** Decode blocks of I-frame
 */
static void vc1_decode_i_blocks(VC1Context *v)
{
    int k;
    MpegEncContext *s = &v->s;
    int cbp, val;
    uint8_t *coded_val;
    int mb_pos;

    /* select codingmode used for VLC tables selection */
    switch(v->y_ac_table_index){
    case 0:
        v->codingset = (v->pqindex <= 8) ? CS_HIGH_RATE_INTRA : CS_LOW_MOT_INTRA;
        break;
    case 1:
        v->codingset = CS_HIGH_MOT_INTRA;
        break;
    case 2:
        v->codingset = CS_MID_RATE_INTRA;
        break;
    }

    switch(v->c_ac_table_index){
    case 0:
        v->codingset2 = (v->pqindex <= 8) ? CS_HIGH_RATE_INTER : CS_LOW_MOT_INTER;
        break;
    case 1:
        v->codingset2 = CS_HIGH_MOT_INTER;
        break;
    case 2:
        v->codingset2 = CS_MID_RATE_INTER;
        break;
    }

    /* Set DC scale - y and c use the same */
    s->y_dc_scale = s->y_dc_scale_table[v->pq];
    s->c_dc_scale = s->c_dc_scale_table[v->pq];

    //do frame decode
    s->mb_x = s->mb_y = 0;
    s->mb_intra = 1;
    ff_er_add_slice(s, 0, 0, s->mb_width - 1, s->mb_height - 1, (AC_END|DC_END|MV_END));
    for(s->mb_y = 0; s->mb_y < s->mb_height; s->mb_y++) {
        for(s->mb_x = 0; s->mb_x < s->mb_width; s->mb_x++) {
            ff_init_block_index(s);
            ff_update_block_index(s);
            s->dsp.clear_blocks(s->block[0]);
            mb_pos = s->mb_x + s->mb_y * s->mb_width;
            s->current_picture.mb_type[mb_pos] = MB_TYPE_INTRA;
            s->current_picture.qscale_table[mb_pos] = v->pq;

            // do actual MB decoding and displaying
            cbp = get_vlc2(&v->s.gb, ff_msmp4_mb_i_vlc.table, MB_INTRA_VLC_BITS, 2);
            v->s.ac_pred = get_bits(&v->s.gb, 1);

            for(k = 0; k < 6; k++) {
                val = ((cbp >> (5 - k)) & 1);

                if (k < 4) {
                    int pred = vc1_coded_block_pred(&v->s, k, &coded_val);
                    val = val ^ pred;
                    *coded_val = val;
                }
                cbp |= val << (5 - k);

                vc1_decode_i_block(v, s->block[k], k, val, (k<4)? v->codingset : v->codingset2);

                vc1_inv_trans(s->block[k], 8, 8);
                if(v->pq >= 9 && v->overlap) {
                    vc1_overlap_block(s, s->block[k], k, (s->mb_y || k>1), (s->mb_x || (k != 0 && k != 2)));
                }
            }

            vc1_put_block(v, s->block);
            if(v->pq >= 9 && v->overlap) { /* XXX: do proper overlapping insted of loop filter */
                if(s->mb_y) {
                    s->dsp.h263_v_loop_filter(s->dest[0], s->linesize, s->y_dc_scale);
                    s->dsp.h263_v_loop_filter(s->dest[0] + 8, s->linesize, s->y_dc_scale);
                    s->dsp.h263_v_loop_filter(s->dest[1], s->uvlinesize, s->y_dc_scale);
                    s->dsp.h263_v_loop_filter(s->dest[2], s->uvlinesize, s->y_dc_scale);
                }
                s->dsp.h263_v_loop_filter(s->dest[0] + 8 * s->linesize, s->linesize, s->y_dc_scale);
                s->dsp.h263_v_loop_filter(s->dest[0] + 8 * s->linesize + 8, s->linesize, s->y_dc_scale);
                if(s->mb_x) {
                    s->dsp.h263_h_loop_filter(s->dest[0], s->linesize, s->y_dc_scale);
                    s->dsp.h263_h_loop_filter(s->dest[0] + 8 * s->linesize, s->linesize, s->y_dc_scale);
                    s->dsp.h263_h_loop_filter(s->dest[1], s->uvlinesize, s->y_dc_scale);
                    s->dsp.h263_h_loop_filter(s->dest[2], s->uvlinesize, s->y_dc_scale);
                }
                s->dsp.h263_h_loop_filter(s->dest[0] + 8, s->linesize, s->y_dc_scale);
                s->dsp.h263_h_loop_filter(s->dest[0] + 8 * s->linesize + 8, s->linesize, s->y_dc_scale);
            }

            if(get_bits_count(&s->gb) > v->bits) {
                av_log(s->avctx, AV_LOG_ERROR, "Bits overconsumption: %i > %i\n", get_bits_count(&s->gb), v->bits);
                return;
            }
        }
        ff_draw_horiz_band(s, s->mb_y * 16, 16);
    }
}

static void vc1_decode_p_blocks(VC1Context *v)
{
    MpegEncContext *s = &v->s;

    /* select codingmode used for VLC tables selection */
    switch(v->c_ac_table_index){
    case 0:
        v->codingset = (v->pqindex <= 8) ? CS_HIGH_RATE_INTRA : CS_LOW_MOT_INTRA;
        break;
    case 1:
        v->codingset = CS_HIGH_MOT_INTRA;
        break;
    case 2:
        v->codingset = CS_MID_RATE_INTRA;
        break;
    }

    switch(v->c_ac_table_index){
    case 0:
        v->codingset2 = (v->pqindex <= 8) ? CS_HIGH_RATE_INTER : CS_LOW_MOT_INTER;
        break;
    case 1:
        v->codingset2 = CS_HIGH_MOT_INTER;
        break;
    case 2:
        v->codingset2 = CS_MID_RATE_INTER;
        break;
    }

    ff_er_add_slice(s, 0, 0, s->mb_width - 1, s->mb_height - 1, (AC_END|DC_END|MV_END));
    s->first_slice_line = 1;
    for(s->mb_y = 0; s->mb_y < s->mb_height; s->mb_y++) {
        for(s->mb_x = 0; s->mb_x < s->mb_width; s->mb_x++) {
            ff_init_block_index(s);
            ff_update_block_index(s);
            s->dsp.clear_blocks(s->block[0]);

            vc1_decode_p_mb(v, s->block);
            if(get_bits_count(&s->gb) > v->bits || get_bits_count(&s->gb) < 0) {
                av_log(s->avctx, AV_LOG_ERROR, "Bits overconsumption: %i > %i at %ix%i\n", get_bits_count(&s->gb), v->bits,s->mb_x,s->mb_y);
                return;
            }
        }
        ff_draw_horiz_band(s, s->mb_y * 16, 16);
        s->first_slice_line = 0;
    }
}

static void vc1_decode_blocks(VC1Context *v)
{

    v->s.esc3_level_length = 0;

    switch(v->s.pict_type) {
    case I_TYPE:
        vc1_decode_i_blocks(v);
        break;
    case P_TYPE:
        vc1_decode_p_blocks(v);
        break;
    }
}


/** Initialize a VC1/WMV3 decoder
 * @todo TODO: Handle VC-1 IDUs (Transport level?)
 * @todo TODO: Decypher remaining bits in extra_data
 */
static int vc1_decode_init(AVCodecContext *avctx)
{
    VC1Context *v = avctx->priv_data;
    MpegEncContext *s = &v->s;
    GetBitContext gb;

    if (!avctx->extradata_size || !avctx->extradata) return -1;
    avctx->pix_fmt = PIX_FMT_YUV420P;
    v->s.avctx = avctx;

    if(ff_h263_decode_init(avctx) < 0)
        return -1;
    if (vc1_init_common(v) < 0) return -1;

    av_log(avctx, AV_LOG_INFO, "This decoder is not supposed to produce picture. Dont report this as a bug!\n");
    av_log(avctx, AV_LOG_INFO, "If you see a picture, don't believe your eyes.\n");

    avctx->flags |= CODEC_FLAG_EMU_EDGE;
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

    /* Init coded blocks info */
    if (v->profile == PROFILE_ADVANCED)
    {
        if (alloc_bitplane(&v->over_flags_plane, s->mb_width, s->mb_height) < 0)
            return -1;
        if (alloc_bitplane(&v->ac_pred_plane, s->mb_width, s->mb_height) < 0)
            return -1;
    }

    return 0;
}


/** Decode a VC1/WMV3 frame
 * @todo TODO: Handle VC-1 IDUs (Transport level?)
 * @warning Initial try at using MpegEncContext stuff
 */
static int vc1_decode_frame(AVCodecContext *avctx,
                            void *data, int *data_size,
                            uint8_t *buf, int buf_size)
{
    VC1Context *v = avctx->priv_data;
    MpegEncContext *s = &v->s;
    AVFrame *pict = data;

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

    //we need to set current_picture_ptr before reading the header, otherwise we cant store anyting im there
    if(s->current_picture_ptr==NULL || s->current_picture_ptr->data[0]){
        int i= ff_find_unused_picture(s, 0);
        s->current_picture_ptr= &s->picture[i];
    }

    avctx->has_b_frames= !s->low_delay;

    init_get_bits(&s->gb, buf, buf_size*8);
    // do parse frame header
    if(vc1_parse_frame_header(v, &s->gb) == -1)
        return -1;

    if(s->pict_type != I_TYPE && s->pict_type != P_TYPE)return -1;

    // for hurry_up==5
    s->current_picture.pict_type= s->pict_type;
    s->current_picture.key_frame= s->pict_type == I_TYPE;

    /* skip B-frames if we don't have reference frames */
    if(s->last_picture_ptr==NULL && (s->pict_type==B_TYPE || s->dropable)) return -1;//buf_size;
    /* skip b frames if we are in a hurry */
    if(avctx->hurry_up && s->pict_type==B_TYPE) return -1;//buf_size;
    if(   (avctx->skip_frame >= AVDISCARD_NONREF && s->pict_type==B_TYPE)
       || (avctx->skip_frame >= AVDISCARD_NONKEY && s->pict_type!=I_TYPE)
       ||  avctx->skip_frame >= AVDISCARD_ALL)
        return buf_size;
    /* skip everything if we are in a hurry>=5 */
    if(avctx->hurry_up>=5) return -1;//buf_size;

    if(s->next_p_frame_damaged){
        if(s->pict_type==B_TYPE)
            return buf_size;
        else
            s->next_p_frame_damaged=0;
    }

    if(MPV_frame_start(s, avctx) < 0)
        return -1;

    ff_er_frame_start(s);

    v->bits = buf_size * 8;
    vc1_decode_blocks(v);
//av_log(s->avctx, AV_LOG_INFO, "Consumed %i/%i bits\n", get_bits_count(&s->gb), buf_size*8);
//  if(get_bits_count(&s->gb) > buf_size * 8)
//      return -1;
    ff_er_frame_end(s);

    MPV_frame_end(s);

assert(s->current_picture.pict_type == s->current_picture_ptr->pict_type);
assert(s->current_picture.pict_type == s->pict_type);
    if (s->pict_type == B_TYPE || s->low_delay) {
        *pict= *(AVFrame*)s->current_picture_ptr;
    } else if (s->last_picture_ptr != NULL) {
        *pict= *(AVFrame*)s->last_picture_ptr;
    }

    if(s->last_picture_ptr || s->low_delay){
        *data_size = sizeof(AVFrame);
        ff_print_debug_info(s, pict);
    }

    /* Return the Picture timestamp as the frame number */
    /* we substract 1 because it is added on utils.c    */
    avctx->frame_number = s->picture_number - 1;

    return buf_size;
}


/** Close a VC1/WMV3 decoder
 * @warning Initial try at using MpegEncContext stuff
 */
static int vc1_decode_end(AVCodecContext *avctx)
{
    VC1Context *v = avctx->priv_data;

    av_freep(&v->hrd_rate);
    av_freep(&v->hrd_buffer);
    MPV_common_end(&v->s);
    free_bitplane(&v->mv_type_mb_plane);
    free_bitplane(&v->skip_mb_plane);
    free_bitplane(&v->direct_mb_plane);
    return 0;
}


AVCodec vc1_decoder = {
    "vc1",
    CODEC_TYPE_VIDEO,
    CODEC_ID_VC1,
    sizeof(VC1Context),
    vc1_decode_init,
    NULL,
    vc1_decode_end,
    vc1_decode_frame,
    CODEC_CAP_DELAY,
    NULL
};

AVCodec wmv3_decoder = {
    "wmv3",
    CODEC_TYPE_VIDEO,
    CODEC_ID_WMV3,
    sizeof(VC1Context),
    vc1_decode_init,
    NULL,
    vc1_decode_end,
    vc1_decode_frame,
    CODEC_CAP_DELAY,
    NULL
};
