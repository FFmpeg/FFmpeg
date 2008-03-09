/*
 * VC-1 and WMV3 decoder
 * Copyright (c) 2006-2007 Konstantin Shishkov
 * Partly based on vc9.c (c) 2005 Anonymous, Alex Beregszaszi, Michael Niedermayer
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

#ifndef FFMPEG_VC1_H
#define FFMPEG_VC1_H

#include "avcodec.h"
#include "mpegvideo.h"
#include "intrax8.h"

/** Markers used in VC-1 AP frame data */
//@{
enum VC1Code{
    VC1_CODE_RES0       = 0x00000100,
    VC1_CODE_ENDOFSEQ   = 0x0000010A,
    VC1_CODE_SLICE,
    VC1_CODE_FIELD,
    VC1_CODE_FRAME,
    VC1_CODE_ENTRYPOINT,
    VC1_CODE_SEQHDR,
};
//@}

#define IS_MARKER(x) (((x) & ~0xFF) == VC1_CODE_RES0)

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
    BMV_TYPE_INTERPOLATED
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

/** @name Overlap conditions for Advanced Profile */
//@{
enum COTypes {
    CONDOVER_NONE = 0,
    CONDOVER_ALL,
    CONDOVER_SELECT
};
//@}


/** The VC1 Context
 * @todo Change size wherever another size is more efficient
 * Many members are only used for Advanced Profile
 */
typedef struct VC1Context{
    MpegEncContext s;
    IntraX8Context x8;

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
    int psf;              ///< Progressive Segmented Frame
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
    const uint8_t* zz_8x4;///< Zigzag scan table for TT_8x4 coding mode
    const uint8_t* zz_4x8;///< Zigzag scan table for TT_4x8 coding mode
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
    uint8_t ttblk4x4;     ///< Value of ttblk which indicates a 4x4 transform
    int codingset;        ///< index of current table set from 11.8 to use for luma block decoding
    int codingset2;       ///< index of current table set from 11.8 to use for chroma block decoding
    int pqindex;          ///< raw pqindex used in coding set selection
    int a_avail, c_avail;
    uint8_t *mb_type_base, *mb_type[3];


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
    VLC *cbpcy_vlc;               ///< CBPCY VLC table
    int tt_index;                 ///< Index for Transform Type tables
    uint8_t* mv_type_mb_plane;    ///< bitplane for mv_type == (4MV)
    uint8_t* direct_mb_plane;     ///< bitplane for "direct" MBs
    int mv_type_is_raw;           ///< mv type mb plane is not coded
    int dmb_is_raw;               ///< direct mb plane is raw
    int skip_is_raw;              ///< skip mb plane is not coded
    uint8_t luty[256], lutuv[256]; // lookup tables used for intensity compensation
    int use_ic;                   ///< use intensity compensation in B-frames
    int rnd;                      ///< rounding control

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
    uint8_t* acpred_plane;       ///< AC prediction flags bitplane
    int acpred_is_raw;
    uint8_t* over_flags_plane;   ///< Overflags bitplane
    int overflg_is_raw;
    uint8_t condover;
    uint16_t *hrd_rate, *hrd_buffer;
    uint8_t *hrd_fullness;
    uint8_t range_mapy_flag;
    uint8_t range_mapuv_flag;
    uint8_t range_mapy;
    uint8_t range_mapuv;
    //@}

    int p_frame_skipped;
    int bi_type;
    int x8_type;
} VC1Context;

#endif /* FFMPEG_VC1_H */
