/*
 * Generic DCT based hybrid video encoder
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard.
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
 */

/**
 * @file mpegvideo.h
 * mpegvideo header.
 */
 
#ifndef AVCODEC_MPEGVIDEO_H
#define AVCODEC_MPEGVIDEO_H

#include "dsputil.h"

#define FRAME_SKIPED 100 ///< return value for header parsers if frame is not coded

enum OutputFormat {
    FMT_MPEG1,
    FMT_H263,
    FMT_MJPEG, 
    FMT_H264,
};

#define EDGE_WIDTH 16

#define MPEG_BUF_SIZE (16 * 1024)

#define QMAT_SHIFT_MMX 16
#define QMAT_SHIFT 22

#define MAX_FCODE 7
#define MAX_MV 2048

#define MAX_PICTURE_COUNT 15

#define ME_MAP_SIZE 64
#define ME_MAP_SHIFT 3
#define ME_MAP_MV_BITS 11

/* run length table */
#define MAX_RUN    64
#define MAX_LEVEL  64

#define I_TYPE FF_I_TYPE  ///< Intra
#define P_TYPE FF_P_TYPE  ///< Predicted
#define B_TYPE FF_B_TYPE  ///< Bi-dir predicted
#define S_TYPE FF_S_TYPE  ///< S(GMC)-VOP MPEG4
#define SI_TYPE FF_SI_TYPE  ///< Switching Intra
#define SP_TYPE FF_SP_TYPE  ///< Switching Predicted

typedef struct Predictor{
    double coeff;
    double count;
    double decay;
} Predictor;

typedef struct RateControlEntry{
    int pict_type;
    float qscale;
    int mv_bits;
    int i_tex_bits;
    int p_tex_bits;
    int misc_bits;
    uint64_t expected_bits;
    int new_pict_type;
    float new_qscale;
    int mc_mb_var_sum;
    int mb_var_sum;
    int i_count;
    int f_code;
    int b_code;
}RateControlEntry;

/**
 * rate control context.
 */
typedef struct RateControlContext{
    FILE *stats_file;
    int num_entries;              ///< number of RateControlEntries 
    RateControlEntry *entry;
    int buffer_index;             ///< amount of bits in the video/audio buffer 
    Predictor pred[5];
    double short_term_qsum;       ///< sum of recent qscales 
    double short_term_qcount;     ///< count of recent qscales 
    double pass1_rc_eq_output_sum;///< sum of the output of the rc equation, this is used for normalization  
    double pass1_wanted_bits;     ///< bits which should have been outputed by the pass1 code (including complexity init) 
    double last_qscale;
    double last_qscale_for[5];    ///< last qscale for a specific pict type, used for max_diff & ipb factor stuff 
    int last_mc_mb_var_sum;
    int last_mb_var_sum;
    uint64_t i_cplx_sum[5];
    uint64_t p_cplx_sum[5];
    uint64_t mv_bits_sum[5];
    uint64_t qscale_sum[5];
    int frame_count[5];
    int last_non_b_pict_type;
}RateControlContext;

/**
 * Scantable.
 */
typedef struct ScanTable{
    const uint8_t *scantable;
    uint8_t permutated[64];
    uint8_t raster_end[64];
#ifdef ARCH_POWERPC
		/** Used by dct_quantise_alitvec to find last-non-zero */
    uint8_t __align8 inverse[64];
#endif
} ScanTable;

/**
 * Picture.
 */
typedef struct Picture{
    FF_COMMON_FRAME

    /**
     * halfpel luma planes.
     */
    uint8_t *interpolated[3];
    
    int16_t (*motion_val[2])[2];
    int8_t *ref_index[2];
    uint32_t *mb_type_base;
    uint32_t *mb_type;           ///< mb_type_base + mb_width + 2, note: only used for decoding currently
#define MB_TYPE_INTRA4x4   0x0001
#define MB_TYPE_INTRA16x16 0x0002 //FIXME h264 specific
#define MB_TYPE_INTRA_PCM  0x0004 //FIXME h264 specific
#define MB_TYPE_16x16      0x0008
#define MB_TYPE_16x8       0x0010
#define MB_TYPE_8x16       0x0020
#define MB_TYPE_8x8        0x0040
#define MB_TYPE_INTERLACED 0x0080
#define MB_TYPE_DIRECT2     0x0100 //FIXME
#define MB_TYPE_ACPRED     0x0200
#define MB_TYPE_GMC        0x0400 //FIXME mpeg4 specific
#define MB_TYPE_SKIP       0x0800
#define MB_TYPE_P0L0       0x1000
#define MB_TYPE_P1L0       0x2000
#define MB_TYPE_P0L1       0x4000
#define MB_TYPE_P1L1       0x8000
#define MB_TYPE_L0         (MB_TYPE_P0L0 | MB_TYPE_P1L0)
#define MB_TYPE_L1         (MB_TYPE_P0L1 | MB_TYPE_P1L1)
#define MB_TYPE_L0L1       (MB_TYPE_L0   | MB_TYPE_L1)
#define MB_TYPE_QUANT      0x00010000
//Note bits 24-31 are reserved for codec specific use (h264 ref0, mpeg1 pat, ...)

#define IS_INTRA4x4(a)   ((a)&MB_TYPE_INTRA4x4)
#define IS_INTRA16x16(a) ((a)&MB_TYPE_INTRA16x16)
#define IS_PCM(a)        ((a)&MB_TYPE_INTRA_PCM)
#define IS_INTRA(a)      ((a)&7)
#define IS_INTER(a)      ((a)&(MB_TYPE_16x16|MB_TYPE_16x8|MB_TYPE_8x16|MB_TYPE_8x8))
#define IS_SKIP(a)       ((a)&MB_TYPE_SKIP)
#define IS_INTRA_PCM(a)  ((a)&MB_TYPE_INTRA_PCM)
#define IS_INTERLACED(a) ((a)&MB_TYPE_INTERLACED)
#define IS_DIRECT(a)     ((a)&MB_TYPE_DIRECT2)
#define IS_GMC(a)        ((a)&MB_TYPE_GMC)
#define IS_16X16(a)      ((a)&MB_TYPE_16x16)
#define IS_16X8(a)       ((a)&MB_TYPE_16x8)
#define IS_8X16(a)       ((a)&MB_TYPE_8x16)
#define IS_8X8(a)        ((a)&MB_TYPE_8x8)
#define IS_SUB_8X8(a)    ((a)&MB_TYPE_16x16) //note reused
#define IS_SUB_8X4(a)    ((a)&MB_TYPE_16x8)  //note reused
#define IS_SUB_4X8(a)    ((a)&MB_TYPE_8x16)  //note reused
#define IS_SUB_4X4(a)    ((a)&MB_TYPE_8x8)   //note reused
#define IS_ACPRED(a)     ((a)&MB_TYPE_ACPRED)
#define IS_QUANT(a)      ((a)&MB_TYPE_QUANT)
#define IS_DIR(a, part, list) ((a) & (MB_TYPE_P0L0<<((part)+2*(list))))
#define USES_LIST(a, list) ((a) & ((MB_TYPE_P0L0|MB_TYPE_P1L0)<<(2*(list)))) ///< does this mb use listX, note doesnt work if subMBs


    int field_poc[2];           ///< h264 top/bottom POC
    int poc;                    ///< h264 frame POC
    int frame_num;              ///< h264 frame_num
    int pic_id;                 ///< h264 pic_num or long_term_pic_idx
    int long_ref;               ///< 1->long term reference 0->short term reference

    int mb_var_sum;             ///< sum of MB variance for current frame 
    int mc_mb_var_sum;          ///< motion compensated MB variance for current frame 
    uint16_t *mb_var;           ///< Table for MB variances 
    uint16_t *mc_mb_var;        ///< Table for motion compensated MB variances 
    uint8_t *mb_mean;           ///< Table for MB luminance 
    int32_t *mb_cmp_score;	///< Table for MB cmp scores, for mb decission 
    int b_frame_score;          /* */
} Picture;

typedef struct ParseContext{
    uint8_t *buffer;
    int index;
    int last_index;
    int buffer_size;
    uint32_t state;             ///< contains the last few bytes in MSB order
    int frame_start_found;
    int overread;               ///< the number of bytes which where irreversibly read from the next frame
    int overread_index;         ///< the index into ParseContext.buffer of the overreaded bytes
} ParseContext;

struct MpegEncContext;

/**
 * Motion estimation context.
 */
typedef struct MotionEstContext{
    int skip;                          ///< set if ME is skiped for the current MB 
    int co_located_mv[4][2];           ///< mv from last p frame for direct mode ME 
    int direct_basis_mv[4][2];
    uint8_t *scratchpad;               ///< data area for the me algo, so that the ME doesnt need to malloc/free 
    uint32_t *map;                     ///< map to avoid duplicate evaluations 
    uint32_t *score_map;               ///< map to store the scores 
    int map_generation;  
    int pre_penalty_factor;
    int penalty_factor;
    int sub_penalty_factor;
    int mb_penalty_factor;
    int pre_pass;                      ///< = 1 for the pre pass 
    int dia_size;
    uint8_t (*mv_penalty)[MAX_MV*2+1];  ///< amount of bits needed to encode a MV 
    int (*sub_motion_search)(struct MpegEncContext * s,
				  int *mx_ptr, int *my_ptr, int dmin,
				  int xmin, int ymin, int xmax, int ymax,
                                  int pred_x, int pred_y, Picture *ref_picture, 
                                  int n, int size, uint8_t * const mv_penalty);
    int (*motion_search[7])(struct MpegEncContext * s, int block,
                             int *mx_ptr, int *my_ptr,
                             int P[10][2], int pred_x, int pred_y,
                             int xmin, int ymin, int xmax, int ymax, Picture *ref_picture, int16_t (*last_mv)[2], 
                             int ref_mv_scale, uint8_t * const mv_penalty);
    int (*pre_motion_search)(struct MpegEncContext * s, int block,
                             int *mx_ptr, int *my_ptr,
                             int P[10][2], int pred_x, int pred_y,
                             int xmin, int ymin, int xmax, int ymax, Picture *ref_picture, int16_t (*last_mv)[2], 
                             int ref_mv_scale, uint8_t * const mv_penalty);
    int (*get_mb_score)(struct MpegEncContext * s, int mx, int my, int pred_x, int pred_y, Picture *ref_picture, 
                                  uint8_t * const mv_penalty);
}MotionEstContext;

/**
 * MpegEncContext.
 */
typedef struct MpegEncContext {
    struct AVCodecContext *avctx;
    /* the following parameters must be initialized before encoding */
    int width, height;///< picture size. must be a multiple of 16 
    int gop_size;
    int intra_only;   ///< if true, only intra pictures are generated 
    int bit_rate;     ///< wanted bit rate 
    int bit_rate_tolerance; ///< amount of +- bits (>0)
    enum OutputFormat out_format; ///< output format 
    int h263_pred;    ///< use mpeg4/h263 ac/dc predictions 

/* the following codec id fields are deprecated in favor of codec_id */
    int h263_plus;    ///< h263 plus headers 
    int h263_rv10;    ///< use RV10 variation for H263 
    int h263_msmpeg4; ///< generate MSMPEG4 compatible stream (deprecated, use msmpeg4_version instead)
    int h263_intel;   ///< use I263 intel h263 header 
    int h263_flv;     ///< use flv h263 header 
    
    int codec_id;     /* see CODEC_ID_xxx */
    int fixed_qscale; ///< fixed qscale if non zero 
    float qcompress;  ///< amount of qscale change between easy & hard scenes (0.0-1.0) 
    float qblur;      ///< amount of qscale smoothing over time (0.0-1.0) 
    int max_qdiff;    ///< max qscale difference between frames 
    int encoding;     ///< true if we are encoding (vs decoding) 
    int flags;        ///< AVCodecContext.flags (HQ, MV4, ...) 
    int max_b_frames; ///< max number of b-frames for encoding 
    int b_frame_strategy;
    int luma_elim_threshold;
    int chroma_elim_threshold;
    int strict_std_compliance; ///< strictly follow the std (MPEG4, ...) 
    int workaround_bugs;       ///< workaround bugs in encoders which cannot be detected automatically 
    /* the following fields are managed internally by the encoder */

    /** bit output */
    PutBitContext pb;

    /* sequence parameters */
    int context_initialized;
    int input_picture_number;
    int picture_number;
    int picture_in_gop_number; ///< 0-> first pic in gop, ... 
    int b_frames_since_non_b;  ///< used for encoding, relative to not yet reordered input 
    int mb_width, mb_height;   ///< number of MBs horizontally & vertically 
    int mb_stride;             ///< mb_width+1 used for some arrays to allow simple addressng of left & top MBs withoutt sig11
    int h_edge_pos, v_edge_pos;///< horizontal / vertical position of the right/bottom edge (pixel replicateion)
    int mb_num;                ///< number of MBs of a picture 
    int linesize;              ///< line size, in bytes, may be different from width 
    int uvlinesize;            ///< line size, for chroma in bytes, may be different from width 
    Picture picture[MAX_PICTURE_COUNT]; ///< main picture buffer 
    Picture *input_picture[MAX_PICTURE_COUNT]; ///< next pictures on display order for encoding
    Picture *reordered_input_picture[MAX_PICTURE_COUNT]; ///< pointer to the next pictures in codedorder for encoding
    
    /** 
     * copy of the previous picture structure.
     * note, linesize & data, might not match the previous picture (for field pictures)
     */
    Picture last_picture;       
    
    /** 
     * copy of the next picture structure.
     * note, linesize & data, might not match the next picture (for field pictures)
     */
    Picture next_picture;
    
    /** 
     * copy of the source picture structure for encoding.
     * note, linesize & data, might not match the source picture (for field pictures)
     */
    Picture new_picture;
    
    /** 
     * copy of the current picture structure.
     * note, linesize & data, might not match the current picture (for field pictures)
     */
    Picture current_picture;    ///< buffer to store the decompressed current picture 
    
    Picture *last_picture_ptr;     ///< pointer to the previous picture.
    Picture *next_picture_ptr;     ///< pointer to the next picture (for bidir pred) 
    Picture *new_picture_ptr;      ///< pointer to the source picture for encoding 
    Picture *current_picture_ptr;  ///< pointer to the current picture
    int last_dc[3];                ///< last DC values for MPEG1 
    int16_t *dc_val[3];            ///< used for mpeg4 DC prediction, all 3 arrays must be continuous 
    int16_t dc_cache[4*5];
    int y_dc_scale, c_dc_scale;
    uint8_t *y_dc_scale_table;     ///< qscale -> y_dc_scale table 
    uint8_t *c_dc_scale_table;     ///< qscale -> c_dc_scale table 
    uint8_t *coded_block;          ///< used for coded block pattern prediction (msmpeg4v3, wmv1)
    int16_t (*ac_val[3])[16];      ///< used for for mpeg4 AC prediction, all 3 arrays must be continuous 
    int ac_pred;
    uint8_t *prev_pict_types;     ///< previous picture types in bitstream order, used for mb skip 
#define PREV_PICT_TYPES_BUFFER_SIZE 256
    int mb_skiped;                ///< MUST BE SET only during DECODING 
    uint8_t *mbskip_table;        /**< used to avoid copy if macroblock skipped (for black regions for example) 
                                   and used for b-frame encoding & decoding (contains skip table of next P Frame) */
    uint8_t *mbintra_table;       ///< used to avoid setting {ac, dc, cbp}-pred stuff to zero on inter MB decoding 
    uint8_t *cbp_table;           ///< used to store cbp, ac_pred for partitioned decoding 
    uint8_t *pred_dir_table;      ///< used to store pred_dir for partitioned decoding 
    uint8_t *allocated_edge_emu_buffer;
    uint8_t *edge_emu_buffer;     ///< points into the middle of allocated_edge_emu_buffer  

    int qscale;                 ///< QP 
    float frame_qscale;         ///< qscale from the frame level rc FIXME remove
    int adaptive_quant;         ///< use adaptive quantization 
    int dquant;                 ///< qscale difference to prev qscale  
    int pict_type;              ///< I_TYPE, P_TYPE, B_TYPE, ... 
    int last_pict_type;
    int last_non_b_pict_type;   ///< used for mpeg4 gmc b-frames & ratecontrol 
    int frame_rate_index;
    /* motion compensation */
    int unrestricted_mv;        ///< mv can point outside of the coded picture 
    int h263_long_vectors;      ///< use horrible h263v1 long vector mode 
    int decode;                 ///< if 0 then decoding will be skiped (for encoding b frames for example)

    DSPContext dsp;             ///< pointers for accelerated dsp fucntions 
    int f_code;                 ///< forward MV resolution 
    int b_code;                 ///< backward MV resolution for B Frames (mpeg4) 
    int16_t (*motion_val)[2];
    int16_t (*p_mv_table_base)[2];
    int16_t (*b_forw_mv_table_base)[2];
    int16_t (*b_back_mv_table_base)[2];
    int16_t (*b_bidir_forw_mv_table_base)[2]; 
    int16_t (*b_bidir_back_mv_table_base)[2]; 
    int16_t (*b_direct_mv_table_base)[2];
    int16_t (*p_mv_table)[2];            ///< MV table (1MV per MB) p-frame encoding 
    int16_t (*b_forw_mv_table)[2];       ///< MV table (1MV per MB) forward mode b-frame encoding 
    int16_t (*b_back_mv_table)[2];       ///< MV table (1MV per MB) backward mode b-frame encoding 
    int16_t (*b_bidir_forw_mv_table)[2]; ///< MV table (1MV per MB) bidir mode b-frame encoding 
    int16_t (*b_bidir_back_mv_table)[2]; ///< MV table (1MV per MB) bidir mode b-frame encoding 
    int16_t (*b_direct_mv_table)[2];     ///< MV table (1MV per MB) direct mode b-frame encoding 
    int me_method;                       ///< ME algorithm 
    int scene_change_score;
    int mv_dir;
#define MV_DIR_BACKWARD  1
#define MV_DIR_FORWARD   2
#define MV_DIRECT        4 ///< bidirectional mode where the difference equals the MV of the last P/S/I-Frame (mpeg4)
    int mv_type;
#define MV_TYPE_16X16       0   ///< 1 vector for the whole mb 
#define MV_TYPE_8X8         1   ///< 4 vectors (h263, mpeg4 4MV) 
#define MV_TYPE_16X8        2   ///< 2 vectors, one per 16x8 block  
#define MV_TYPE_FIELD       3   ///< 2 vectors, one per field  
#define MV_TYPE_DMV         4   ///< 2 vectors, special mpeg2 Dual Prime Vectors 
    /**motion vectors for a macroblock 
       first coordinate : 0 = forward 1 = backward
       second "         : depend on type
       third  "         : 0 = x, 1 = y
    */
    int mv[2][4][2];
    int field_select[2][2];
    int last_mv[2][2][2];             ///< last MV, used for MV prediction in MPEG1 & B-frame MPEG4 
    uint8_t *fcode_tab;               ///< smallest fcode needed for each MV 
    
    MotionEstContext me;

    int no_rounding;  /**< apply no rounding to motion compensation (MPEG4, msmpeg4, ...) 
                        for b-frames rounding mode is allways 0 */

    int hurry_up;     /**< when set to 1 during decoding, b frames will be skiped
                         when set to 2 idct/dequant will be skipped too */
                        
    /* macroblock layer */
    int mb_x, mb_y;
    int mb_skip_run;
    int mb_intra;
    uint8_t *mb_type;       ///< Table for MB type FIXME remove and use picture->mb_type
#define MB_TYPE_INTRA    0x01
#define MB_TYPE_INTER    0x02
#define MB_TYPE_INTER4V  0x04
#define MB_TYPE_SKIPED   0x08
//#define MB_TYPE_GMC      0x10

#define MB_TYPE_DIRECT   0x10
#define MB_TYPE_FORWARD  0x20
#define MB_TYPE_BACKWARD 0x40
#define MB_TYPE_BIDIR    0x80

    int block_index[6]; ///< index to current MB in block based arrays with edges
    int block_wrap[6];
    uint8_t *dest[3];
    
    int *mb_index2xy;        ///< mb_index -> mb_x + mb_y*mb_stride

    /** matrix transmitted in the bitstream */
    uint16_t intra_matrix[64];
    uint16_t chroma_intra_matrix[64];
    uint16_t inter_matrix[64];
    uint16_t chroma_inter_matrix[64];
#define QUANT_BIAS_SHIFT 8
    int intra_quant_bias;    ///< bias for the quantizer 
    int inter_quant_bias;    ///< bias for the quantizer 
    int min_qcoeff;          ///< minimum encodable coefficient 
    int max_qcoeff;          ///< maximum encodable coefficient 
    int ac_esc_length;       ///< num of bits needed to encode the longest esc 
    uint8_t *intra_ac_vlc_length;
    uint8_t *intra_ac_vlc_last_length;
    uint8_t *inter_ac_vlc_length;
    uint8_t *inter_ac_vlc_last_length;
    uint8_t *luma_dc_vlc_length;
    uint8_t *chroma_dc_vlc_length;
#define UNI_AC_ENC_INDEX(run,level) ((run)*128 + (level))

    /** precomputed matrix (combine qscale and DCT renorm) */
    int __align8 q_intra_matrix[32][64];
    int __align8 q_inter_matrix[32][64];
    /** identical to the above but for MMX & these are not permutated */
    uint16_t __align8 q_intra_matrix16[32][64];
    uint16_t __align8 q_inter_matrix16[32][64];
    uint16_t __align8 q_intra_matrix16_bias[32][64];
    uint16_t __align8 q_inter_matrix16_bias[32][64];
    int block_last_index[6];  ///< last non zero coefficient in block
    /* scantables */
    ScanTable __align8 intra_scantable;
    ScanTable intra_h_scantable;
    ScanTable intra_v_scantable;
    ScanTable inter_scantable; ///< if inter == intra then intra should be used to reduce tha cache usage

    void *opaque;              ///< private data for the user

    /* bit rate control */
    int I_frame_bits; //FIXME used in mpeg12 ...
    int64_t wanted_bits;
    int64_t total_bits;
    int frame_bits;                ///< bits used for the current frame 
    RateControlContext rc_context; ///< contains stuff only accessed in ratecontrol.c

    /* statistics, used for 2-pass encoding */
    int mv_bits;
    int header_bits;
    int i_tex_bits;
    int p_tex_bits;
    int i_count;
    int f_count;
    int b_count;
    int skip_count;
    int misc_bits; ///< cbp, mb_type
    int last_bits; ///< temp var used for calculating the above vars
    
    /* error concealment / resync */
    int error_count;
    uint8_t *error_status_table;       ///< table of the error status of each MB  
#define VP_START            1          ///< current MB is the first after a resync marker 
#define AC_ERROR            2
#define DC_ERROR            4
#define MV_ERROR            8
#define AC_END              16
#define DC_END              32
#define MV_END              64
//FIXME some prefix?
    
    int resync_mb_x;                 ///< x position of last resync marker 
    int resync_mb_y;                 ///< y position of last resync marker 
    GetBitContext last_resync_gb;    ///< used to search for the next resync marker 
    int mb_num_left;                 ///< number of MBs left in this video packet (for partitioned Slices only)
    int next_p_frame_damaged;        ///< set if the next p frame is damaged, to avoid showing trashed b frames 
    int error_resilience;
    
    ParseContext parse_context;

    /* H.263 specific */
    int gob_number;
    int gob_index;
        
    /* H.263+ specific */
    int umvplus;                    ///< == H263+ && unrestricted_mv 
    int h263_aic;                   ///< Advanded INTRA Coding (AIC) 
    int h263_aic_dir;               ///< AIC direction: 0 = left, 1 = top 
    
    /* mpeg4 specific */
    int time_increment_resolution;
    int time_increment_bits;        ///< number of bits to represent the fractional part of time 
    int last_time_base;
    int time_base;                  ///< time in seconds of last I,P,S Frame 
    int64_t time;                   ///< time of current frame  
    int64_t last_non_b_time;
    uint16_t pp_time;               ///< time distance between the last 2 p,s,i frames 
    uint16_t pb_time;               ///< time distance between the last b and p,s,i frame 
    uint16_t pp_field_time;
    uint16_t pb_field_time;         ///< like above, just for interlaced 
    int shape;
    int vol_sprite_usage;
    int sprite_width;
    int sprite_height;
    int sprite_left;
    int sprite_top;
    int sprite_brightness_change;
    int num_sprite_warping_points;
    int real_sprite_warping_points;
    int sprite_offset[2][2];         ///< sprite offset[isChroma][isMVY] 
    int sprite_delta[2][2];          ///< sprite_delta [isY][isMVY]  
    int sprite_shift[2];             ///< sprite shift [isChroma] 
    int mcsel;
    int quant_precision;
    int quarter_sample;              ///< 1->qpel, 0->half pel ME/MC  
    int scalability;
    int hierachy_type;
    int enhancement_type;
    int new_pred;
    int reduced_res_vop;
    int aspect_ratio_info; //FIXME remove
    int aspected_width;    //FIXME remove
    int aspected_height;   //FIXME remove
    int sprite_warping_accuracy;
    int low_latency_sprite;
    int data_partitioning;           ///< data partitioning flag from header 
    int partitioned_frame;           ///< is current frame partitioned 
    int rvlc;                        ///< reversible vlc 
    int resync_marker;               ///< could this stream contain resync markers
    int low_delay;                   ///< no reordering needed / has no b-frames 
    int vo_type;
    int vol_control_parameters;      ///< does the stream contain the low_delay flag, used to workaround buggy encoders 
    PutBitContext tex_pb;            ///< used for data partitioned VOPs 
    PutBitContext pb2;               ///< used for data partitioned VOPs 
#define PB_BUFFER_SIZE 1024*256
    uint8_t *tex_pb_buffer;          
    uint8_t *pb2_buffer;
    int mpeg_quant;
    int16_t (*field_mv_table)[2][2];   ///< used for interlaced b frame decoding 
    int8_t (*field_select_table)[2];   ///< wtf, no really another table for interlaced b frames 
    int t_frame;                       ///< time distance of first I -> B, used for interlaced b frames 
    int padding_bug_score;             ///< used to detect the VERY common padding bug in MPEG4 

    /* divx specific, used to workaround (many) bugs in divx5 */
    int divx_version;
    int divx_build;
    int divx_packed;
#define BITSTREAM_BUFFER_SIZE 1024*256
    uint8_t *bitstream_buffer; //Divx 5.01 puts several frames in a single one, this is used to reorder them
    int bitstream_buffer_size;
    
    int xvid_build;
    
    /* lavc specific stuff, used to workaround bugs in libavcodec */
    int ffmpeg_version;
    int lavc_build;
    
    /* RV10 specific */
    int rv10_version; ///< RV10 version: 0 or 3 
    int rv10_first_dc_coded[3];
    
    /* MJPEG specific */
    struct MJpegContext *mjpeg_ctx;
    int mjpeg_vsample[3];       ///< vertical sampling factors, default = {2, 1, 1} 
    int mjpeg_hsample[3];       ///< horizontal sampling factors, default = {2, 1, 1} 
    int mjpeg_write_tables;     ///< do we want to have quantisation- and huffmantables in the jpeg file ? 
    int mjpeg_data_only_frames; ///< frames only with SOI, SOS and EOI markers 

    /* MSMPEG4 specific */
    int mv_table_index;
    int rl_table_index;
    int rl_chroma_table_index;
    int dc_table_index;
    int use_skip_mb_code;
    int slice_height;      ///< in macroblocks 
    int first_slice_line;  ///< used in mpeg4 too to handle resync markers 
    int flipflop_rounding;
    int msmpeg4_version;   ///< 0=not msmpeg4, 1=mp41, 2=mp42, 3=mp43/divx3 4=wmv1/7 5=wmv2/8
    int per_mb_rl_table;
    int esc3_level_length;
    int esc3_run_length;
    /** [mb_intra][isChroma][level][run][last] */
    int (*ac_stats)[2][MAX_LEVEL+1][MAX_RUN+1][2];
    int inter_intra_pred;
    int mspel;

    /* decompression specific */
    GetBitContext gb;

    /* Mpeg1 specific */
    int fake_picture_number; ///< picture number at the bitstream frame rate 
    int gop_picture_number;  ///< index of the first picture of a GOP based on fake_pic_num & mpeg1 specific 
    int last_mv_dir;         ///< last mv_dir, used for b frame encoding 
    int broken_link;         ///< no_output_of_prior_pics_flag
    
    /* MPEG2 specific - I wish I had not to support this mess. */
    int progressive_sequence;
    int mpeg_f_code[2][2];
    int picture_structure;
/* picture type */
#define PICT_TOP_FIELD     1
#define PICT_BOTTOM_FIELD  2
#define PICT_FRAME         3

    int intra_dc_precision;
    int frame_pred_frame_dct;
    int top_field_first;
    int concealment_motion_vectors;
    int q_scale_type;
    int intra_vlc_format;
    int alternate_scan;
    int repeat_first_field;
    int chroma_420_type;
    int progressive_frame;
    int mpeg2;
    int full_pel[2];
    int interlaced_dct;
    int first_slice;
    int first_field;         ///< is 1 for the first field of a field picture 0 otherwise
    
    /* RTP specific */
    /* These are explained on avcodec.h */
    int rtp_mode;
    int rtp_payload_size;
    void (*rtp_callback)(void *data, int size, int packet_number);
    uint8_t *ptr_lastgob;
    
    DCTELEM (*block)[64]; ///< points to one of the following blocks 
    DCTELEM blocks[2][6][64] __align8; // for HQ mode we need to keep the best block
    int (*decode_mb)(struct MpegEncContext *s, DCTELEM block[6][64]); // used by some codecs to avoid a switch()
#define SLICE_OK         0
#define SLICE_ERROR     -1
#define SLICE_END       -2 ///<end marker found
#define SLICE_NOEND     -3 ///<no end marker or error found but mb count exceeded
    
    void (*dct_unquantize_mpeg1)(struct MpegEncContext *s, 
                           DCTELEM *block/*align 16*/, int n, int qscale);
    void (*dct_unquantize_mpeg2)(struct MpegEncContext *s, 
                           DCTELEM *block/*align 16*/, int n, int qscale);
    void (*dct_unquantize_h263)(struct MpegEncContext *s, 
                           DCTELEM *block/*align 16*/, int n, int qscale);
    void (*dct_unquantize)(struct MpegEncContext *s, // unquantizer to use (mpeg4 can use both)
                           DCTELEM *block/*align 16*/, int n, int qscale);
    int (*dct_quantize)(struct MpegEncContext *s, DCTELEM *block/*align 16*/, int n, int qscale, int *overflow);
    int (*fast_dct_quantize)(struct MpegEncContext *s, DCTELEM *block/*align 16*/, int n, int qscale, int *overflow);
} MpegEncContext;


int DCT_common_init(MpegEncContext *s);
int MPV_common_init(MpegEncContext *s);
void MPV_common_end(MpegEncContext *s);
void MPV_decode_mb(MpegEncContext *s, DCTELEM block[6][64]);
int MPV_frame_start(MpegEncContext *s, AVCodecContext *avctx);
void MPV_frame_end(MpegEncContext *s);
int MPV_encode_init(AVCodecContext *avctx);
int MPV_encode_end(AVCodecContext *avctx);
int MPV_encode_picture(AVCodecContext *avctx, unsigned char *buf, int buf_size, void *data);
#ifdef HAVE_MMX
void MPV_common_init_mmx(MpegEncContext *s);
#endif
#ifdef ARCH_ALPHA
void MPV_common_init_axp(MpegEncContext *s);
#endif
#ifdef HAVE_MLIB
void MPV_common_init_mlib(MpegEncContext *s);
#endif
#ifdef HAVE_MMI
void MPV_common_init_mmi(MpegEncContext *s);
#endif
#ifdef ARCH_ARMV4L
void MPV_common_init_armv4l(MpegEncContext *s);
#endif
#ifdef ARCH_POWERPC
void MPV_common_init_ppc(MpegEncContext *s);
#endif
extern void (*draw_edges)(uint8_t *buf, int wrap, int width, int height, int w);
void ff_copy_bits(PutBitContext *pb, uint8_t *src, int length);
void ff_clean_intra_table_entries(MpegEncContext *s);
void ff_init_scantable(uint8_t *, ScanTable *st, const uint8_t *src_scantable);
void ff_draw_horiz_band(MpegEncContext *s, int y, int h);
void ff_emulated_edge_mc(uint8_t *buf, uint8_t *src, int linesize, int block_w, int block_h, 
                                    int src_x, int src_y, int w, int h);
#define END_NOT_FOUND -100
int ff_combine_frame( MpegEncContext *s, int next, uint8_t **buf, int *buf_size);
void ff_mpeg_flush(AVCodecContext *avctx);
void ff_print_debug_info(MpegEncContext *s, Picture *pict);
void ff_write_quant_matrix(PutBitContext *pb, int16_t *matrix);

void ff_er_frame_start(MpegEncContext *s);
void ff_er_frame_end(MpegEncContext *s);
void ff_er_add_slice(MpegEncContext *s, int startx, int starty, int endx, int endy, int status);


extern enum PixelFormat ff_yuv420p_list[2];

void ff_init_block_index(MpegEncContext *s);

static inline void ff_update_block_index(MpegEncContext *s){
    s->block_index[0]+=2;
    s->block_index[1]+=2;
    s->block_index[2]+=2;
    s->block_index[3]+=2;
    s->block_index[4]++;
    s->block_index[5]++;
    s->dest[0]+= 16;
    s->dest[1]+= 8;
    s->dest[2]+= 8;
}

static inline int get_bits_diff(MpegEncContext *s){
    const int bits= get_bit_count(&s->pb);
    const int last= s->last_bits;

    s->last_bits = bits;

    return bits - last;
}

/* motion_est.c */
void ff_estimate_p_frame_motion(MpegEncContext * s,
                             int mb_x, int mb_y);
void ff_estimate_b_frame_motion(MpegEncContext * s,
                             int mb_x, int mb_y);
int ff_get_best_fcode(MpegEncContext * s, int16_t (*mv_table)[2], int type);
void ff_fix_long_p_mvs(MpegEncContext * s);
void ff_fix_long_b_mvs(MpegEncContext * s, int16_t (*mv_table)[2], int f_code, int type);
void ff_init_me(MpegEncContext *s);
int ff_pre_estimate_p_frame_motion(MpegEncContext * s, int mb_x, int mb_y);


/* mpeg12.c */
extern const int16_t ff_mpeg1_default_intra_matrix[64];
extern const int16_t ff_mpeg1_default_non_intra_matrix[64];
extern uint8_t ff_mpeg1_dc_scale_table[128];

void mpeg1_encode_picture_header(MpegEncContext *s, int picture_number);
void mpeg1_encode_mb(MpegEncContext *s,
                     DCTELEM block[6][64],
                     int motion_x, int motion_y);
void ff_mpeg1_encode_init(MpegEncContext *s);
void ff_mpeg1_encode_slice_header(MpegEncContext *s);
void ff_mpeg1_clean_buffers(MpegEncContext *s);


/** RLTable. */
typedef struct RLTable {
    int n;                         ///< number of entries of table_vlc minus 1 
    int last;                      ///< number of values for last = 0 
    const uint16_t (*table_vlc)[2];
    const int8_t *table_run;
    const int8_t *table_level;
    uint8_t *index_run[2];         ///< encoding only 
    int8_t *max_level[2];          ///< encoding & decoding 
    int8_t *max_run[2];            ///< encoding & decoding 
    VLC vlc;                       ///< decoding only deprected FIXME remove
    RL_VLC_ELEM *rl_vlc[32];       ///< decoding only 
} RLTable;

void init_rl(RLTable *rl);
void init_vlc_rl(RLTable *rl);

static inline int get_rl_index(const RLTable *rl, int last, int run, int level)
{
    int index;
    index = rl->index_run[last][run];
    if (index >= rl->n)
        return rl->n;
    if (level > rl->max_level[last][run])
        return rl->n;
    return index + level - 1;
}

extern uint8_t ff_mpeg4_y_dc_scale_table[32];
extern uint8_t ff_mpeg4_c_dc_scale_table[32];
extern const int16_t ff_mpeg4_default_intra_matrix[64];
extern const int16_t ff_mpeg4_default_non_intra_matrix[64];
int ff_h263_decode_init(AVCodecContext *avctx);
int ff_h263_decode_frame(AVCodecContext *avctx, 
                             void *data, int *data_size,
                             uint8_t *buf, int buf_size);
int ff_h263_decode_end(AVCodecContext *avctx);
void h263_encode_mb(MpegEncContext *s, 
                    DCTELEM block[6][64],
                    int motion_x, int motion_y);
void mpeg4_encode_mb(MpegEncContext *s, 
                    DCTELEM block[6][64],
                    int motion_x, int motion_y);
void h263_encode_picture_header(MpegEncContext *s, int picture_number);
void ff_flv_encode_picture_header(MpegEncContext *s, int picture_number);
int h263_encode_gob_header(MpegEncContext * s, int mb_line);
int16_t *h263_pred_motion(MpegEncContext * s, int block, 
                        int *px, int *py);
void mpeg4_pred_ac(MpegEncContext * s, DCTELEM *block, int n, 
                   int dir);
void ff_set_mpeg4_time(MpegEncContext * s, int picture_number);
void mpeg4_encode_picture_header(MpegEncContext *s, int picture_number);
void h263_encode_init(MpegEncContext *s);
void h263_decode_init_vlc(MpegEncContext *s);
int h263_decode_picture_header(MpegEncContext *s);
int ff_h263_decode_gob_header(MpegEncContext *s);
int ff_mpeg4_decode_picture_header(MpegEncContext * s, GetBitContext *gb);
void ff_h263_update_motion_val(MpegEncContext * s);


int intel_h263_decode_picture_header(MpegEncContext *s);
int flv_h263_decode_picture_header(MpegEncContext *s);
int ff_h263_decode_mb(MpegEncContext *s,
                      DCTELEM block[6][64]);
int h263_get_picture_format(int width, int height);
void ff_mpeg4_encode_video_packet_header(MpegEncContext *s);
void ff_mpeg4_clean_buffers(MpegEncContext *s);
void ff_mpeg4_stuffing(PutBitContext * pbc);
void ff_mpeg4_init_partitions(MpegEncContext *s);
void ff_mpeg4_merge_partitions(MpegEncContext *s);
void ff_clean_mpeg4_qscales(MpegEncContext *s);
void ff_clean_h263_qscales(MpegEncContext *s);
int ff_mpeg4_decode_partitions(MpegEncContext *s);
int ff_mpeg4_get_video_packet_prefix_length(MpegEncContext *s);
int ff_h263_resync(MpegEncContext *s);
int ff_h263_get_gob_height(MpegEncContext *s);
int ff_mpeg4_set_direct_mv(MpegEncContext *s, int mx, int my);
inline int ff_h263_round_chroma(int x);


/* rv10.c */
void rv10_encode_picture_header(MpegEncContext *s, int picture_number);
int rv_decode_dc(MpegEncContext *s, int n);


/* msmpeg4.c */
void msmpeg4_encode_picture_header(MpegEncContext * s, int picture_number);
void msmpeg4_encode_ext_header(MpegEncContext * s);
void msmpeg4_encode_mb(MpegEncContext * s, 
                       DCTELEM block[6][64],
                       int motion_x, int motion_y);
int msmpeg4_decode_picture_header(MpegEncContext * s);
int msmpeg4_decode_ext_header(MpegEncContext * s, int buf_size);
int ff_msmpeg4_decode_init(MpegEncContext *s);
void ff_msmpeg4_encode_init(MpegEncContext *s);
int ff_wmv2_decode_picture_header(MpegEncContext * s);
void ff_wmv2_add_mb(MpegEncContext *s, DCTELEM block[6][64], uint8_t *dest_y, uint8_t *dest_cb, uint8_t *dest_cr);
void ff_mspel_motion(MpegEncContext *s,
                               uint8_t *dest_y, uint8_t *dest_cb, uint8_t *dest_cr,
                               uint8_t **ref_picture, op_pixels_func (*pix_op)[4],
                               int motion_x, int motion_y, int h);
int ff_wmv2_encode_picture_header(MpegEncContext * s, int picture_number);
void ff_wmv2_encode_mb(MpegEncContext * s, 
                       DCTELEM block[6][64],
                       int motion_x, int motion_y);

/* mjpeg.c */
int mjpeg_init(MpegEncContext *s);
void mjpeg_close(MpegEncContext *s);
void mjpeg_encode_mb(MpegEncContext *s, 
                     DCTELEM block[6][64]);
void mjpeg_picture_header(MpegEncContext *s);
void mjpeg_picture_trailer(MpegEncContext *s);


/* rate control */
int ff_rate_control_init(MpegEncContext *s);
float ff_rate_estimate_qscale(MpegEncContext *s);
void ff_write_pass1_stats(MpegEncContext *s);
void ff_rate_control_uninit(MpegEncContext *s);
double ff_eval(char *s, double *const_value, const char **const_name,
               double (**func1)(void *, double), const char **func1_name,
               double (**func2)(void *, double, double), char **func2_name,
               void *opaque);


#endif /* AVCODEC_MPEGVIDEO_H */
