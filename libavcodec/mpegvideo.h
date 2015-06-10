/*
 * Generic DCT based hybrid video encoder
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer
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
 * mpegvideo header.
 */

#ifndef AVCODEC_MPEGVIDEO_H
#define AVCODEC_MPEGVIDEO_H

#include <float.h>

#include "avcodec.h"
#include "blockdsp.h"
#include "error_resilience.h"
#include "fdctdsp.h"
#include "get_bits.h"
#include "h264chroma.h"
#include "h263dsp.h"
#include "hpeldsp.h"
#include "idctdsp.h"
#include "me_cmp.h"
#include "motion_est.h"
#include "mpegvideodsp.h"
#include "mpegvideoencdsp.h"
#include "pixblockdsp.h"
#include "put_bits.h"
#include "ratecontrol.h"
#include "parser.h"
#include "mpegutils.h"
#include "mpeg12data.h"
#include "qpeldsp.h"
#include "thread.h"
#include "videodsp.h"

#include "libavutil/opt.h"
#include "libavutil/timecode.h"

#define FRAME_SKIPPED 100 ///< return value for header parsers if frame is not coded

#define MAX_FCODE 7

#define MAX_THREADS 32
#define MAX_PICTURE_COUNT 36

#define MAX_B_FRAMES 16

#define ME_MAP_SIZE 64

#define MAX_MB_BYTES (30*16*16*3/8 + 120)

#define INPLACE_OFFSET 16

#define EDGE_WIDTH 16

/* Start codes. */
#define SEQ_END_CODE            0x000001b7
#define SEQ_START_CODE          0x000001b3
#define GOP_START_CODE          0x000001b8
#define PICTURE_START_CODE      0x00000100
#define SLICE_MIN_START_CODE    0x00000101
#define SLICE_MAX_START_CODE    0x000001af
#define EXT_START_CODE          0x000001b5
#define USER_START_CODE         0x000001b2

/**
 * Picture.
 */
typedef struct Picture{
    struct AVFrame *f;
    ThreadFrame tf;

    AVBufferRef *qscale_table_buf;
    int8_t *qscale_table;

    AVBufferRef *motion_val_buf[2];
    int16_t (*motion_val[2])[2];

    AVBufferRef *mb_type_buf;
    uint32_t *mb_type;          ///< types and macros are defined in mpegutils.h

    AVBufferRef *mbskip_table_buf;
    uint8_t *mbskip_table;

    AVBufferRef *ref_index_buf[2];
    int8_t *ref_index[2];

    AVBufferRef *mb_var_buf;
    uint16_t *mb_var;           ///< Table for MB variances

    AVBufferRef *mc_mb_var_buf;
    uint16_t *mc_mb_var;        ///< Table for motion compensated MB variances

    int alloc_mb_width;         ///< mb_width used to allocate tables
    int alloc_mb_height;        ///< mb_height used to allocate tables

    AVBufferRef *mb_mean_buf;
    uint8_t *mb_mean;           ///< Table for MB luminance

    AVBufferRef *hwaccel_priv_buf;
    /**
     * hardware accelerator private data
     */
    void *hwaccel_picture_private;

    int field_picture;          ///< whether or not the picture was encoded in separate fields

    int64_t mb_var_sum;         ///< sum of MB variance for current frame
    int64_t mc_mb_var_sum;      ///< motion compensated MB variance for current frame

    int b_frame_score;
    int needs_realloc;          ///< Picture needs to be reallocated (eg due to a frame size change)

    int reference;
    int shared;

    uint64_t error[AV_NUM_DATA_POINTERS];
} Picture;

typedef struct ScratchpadContext {
    uint8_t *edge_emu_buffer;     ///< temporary buffer for if MVs point to out-of-frame data
    uint8_t *rd_scratchpad;       ///< scratchpad for rate distortion mb decision
    uint8_t *obmc_scratchpad;
    uint8_t *b_scratchpad;        ///< scratchpad used for writing into write only buffers
} ScratchpadContext;

/**
 * MpegEncContext.
 */
typedef struct MpegEncContext {
    AVClass *class;

    int y_dc_scale, c_dc_scale;
    int ac_pred;
    int block_last_index[12];  ///< last non zero coefficient in block
    int h263_aic;              ///< Advanded INTRA Coding (AIC)

    /* scantables */
    ScanTable inter_scantable; ///< if inter == intra then intra should be used to reduce tha cache usage
    ScanTable intra_scantable;
    ScanTable intra_h_scantable;
    ScanTable intra_v_scantable;

    /* WARNING: changes above this line require updates to hardcoded
     *          offsets used in asm. */

    struct AVCodecContext *avctx;
    /* the following parameters must be initialized before encoding */
    int width, height;///< picture size. must be a multiple of 16
    int gop_size;
    int intra_only;   ///< if true, only intra pictures are generated
    int bit_rate;     ///< wanted bit rate
    enum OutputFormat out_format; ///< output format
    int h263_pred;    ///< use mpeg4/h263 ac/dc predictions
    int pb_frame;     ///< PB frame mode (0 = none, 1 = base, 2 = improved)

/* the following codec id fields are deprecated in favor of codec_id */
    int h263_plus;    ///< h263 plus headers
    int h263_flv;     ///< use flv h263 header

    enum AVCodecID codec_id;     /* see AV_CODEC_ID_xxx */
    int fixed_qscale; ///< fixed qscale if non zero
    int encoding;     ///< true if we are encoding (vs decoding)
    int max_b_frames; ///< max number of b-frames for encoding
    int luma_elim_threshold;
    int chroma_elim_threshold;
    int strict_std_compliance; ///< strictly follow the std (MPEG4, ...)
    int workaround_bugs;       ///< workaround bugs in encoders which cannot be detected automatically
    int codec_tag;             ///< internal codec_tag upper case converted from avctx codec_tag
    /* the following fields are managed internally by the encoder */

    /* sequence parameters */
    int context_initialized;
    int input_picture_number;  ///< used to set pic->display_picture_number, should not be used for/by anything else
    int coded_picture_number;  ///< used to set pic->coded_picture_number, should not be used for/by anything else
    int picture_number;       //FIXME remove, unclear definition
    int picture_in_gop_number; ///< 0-> first pic in gop, ...
    int mb_width, mb_height;   ///< number of MBs horizontally & vertically
    int mb_stride;             ///< mb_width+1 used for some arrays to allow simple addressing of left & top MBs without sig11
    int b8_stride;             ///< 2*mb_width+1 used for some 8x8 block arrays to allow simple addressing
    int h_edge_pos, v_edge_pos;///< horizontal / vertical position of the right/bottom edge (pixel replication)
    int mb_num;                ///< number of MBs of a picture
    ptrdiff_t linesize;        ///< line size, in bytes, may be different from width
    ptrdiff_t uvlinesize;      ///< line size, for chroma in bytes, may be different from width
    Picture *picture;          ///< main picture buffer
    Picture **input_picture;   ///< next pictures on display order for encoding
    Picture **reordered_input_picture; ///< pointer to the next pictures in codedorder for encoding

    int64_t user_specified_pts; ///< last non-zero pts from AVFrame which was passed into avcodec_encode_video2()
    /**
     * pts difference between the first and second input frame, used for
     * calculating dts of the first frame when there's a delay */
    int64_t dts_delta;
    /**
     * reordered pts to be used as dts for the next output frame when there's
     * a delay */
    int64_t reordered_pts;

    /** bit output */
    PutBitContext pb;

    int start_mb_y;            ///< start mb_y of this thread (so current thread should process start_mb_y <= row < end_mb_y)
    int end_mb_y;              ///< end   mb_y of this thread (so current thread should process start_mb_y <= row < end_mb_y)
    struct MpegEncContext *thread_context[MAX_THREADS];
    int slice_context_count;   ///< number of used thread_contexts

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
    Picture *current_picture_ptr;  ///< pointer to the current picture
    int last_dc[3];                ///< last DC values for MPEG1
    int16_t *dc_val_base;
    int16_t *dc_val[3];            ///< used for mpeg4 DC prediction, all 3 arrays must be continuous
    const uint8_t *y_dc_scale_table;     ///< qscale -> y_dc_scale table
    const uint8_t *c_dc_scale_table;     ///< qscale -> c_dc_scale table
    const uint8_t *chroma_qscale_table;  ///< qscale -> chroma_qscale (h263)
    uint8_t *coded_block_base;
    uint8_t *coded_block;          ///< used for coded block pattern prediction (msmpeg4v3, wmv1)
    int16_t (*ac_val_base)[16];
    int16_t (*ac_val[3])[16];      ///< used for mpeg4 AC prediction, all 3 arrays must be continuous
    int mb_skipped;                ///< MUST BE SET only during DECODING
    uint8_t *mbskip_table;        /**< used to avoid copy if macroblock skipped (for black regions for example)
                                   and used for b-frame encoding & decoding (contains skip table of next P Frame) */
    uint8_t *mbintra_table;       ///< used to avoid setting {ac, dc, cbp}-pred stuff to zero on inter MB decoding
    uint8_t *cbp_table;           ///< used to store cbp, ac_pred for partitioned decoding
    uint8_t *pred_dir_table;      ///< used to store pred_dir for partitioned decoding

    ScratchpadContext sc;

    int qscale;                 ///< QP
    int chroma_qscale;          ///< chroma QP
    unsigned int lambda;        ///< lagrange multipler used in rate distortion
    unsigned int lambda2;       ///< (lambda*lambda) >> FF_LAMBDA_SHIFT
    int *lambda_table;
    int adaptive_quant;         ///< use adaptive quantization
    int dquant;                 ///< qscale difference to prev qscale
    int closed_gop;             ///< MPEG1/2 GOP is closed
    int pict_type;              ///< AV_PICTURE_TYPE_I, AV_PICTURE_TYPE_P, AV_PICTURE_TYPE_B, ...
    int vbv_delay;
    int last_pict_type; //FIXME removes
    int last_non_b_pict_type;   ///< used for mpeg4 gmc b-frames & ratecontrol
    int droppable;
    int frame_rate_index;
    AVRational mpeg2_frame_rate_ext;
    int last_lambda_for[5];     ///< last lambda for a specific pict type
    int skipdct;                ///< skip dct and code zero residual

    /* motion compensation */
    int unrestricted_mv;        ///< mv can point outside of the coded picture
    int h263_long_vectors;      ///< use horrible h263v1 long vector mode

    BlockDSPContext bdsp;
    FDCTDSPContext fdsp;
    H264ChromaContext h264chroma;
    HpelDSPContext hdsp;
    IDCTDSPContext idsp;
    MECmpContext mecc;
    MpegVideoDSPContext mdsp;
    MpegvideoEncDSPContext mpvencdsp;
    PixblockDSPContext pdsp;
    QpelDSPContext qdsp;
    VideoDSPContext vdsp;
    H263DSPContext h263dsp;
    int f_code;                 ///< forward MV resolution
    int b_code;                 ///< backward MV resolution for B Frames (mpeg4)
    int16_t (*p_mv_table_base)[2];
    int16_t (*b_forw_mv_table_base)[2];
    int16_t (*b_back_mv_table_base)[2];
    int16_t (*b_bidir_forw_mv_table_base)[2];
    int16_t (*b_bidir_back_mv_table_base)[2];
    int16_t (*b_direct_mv_table_base)[2];
    int16_t (*p_field_mv_table_base[2][2])[2];
    int16_t (*b_field_mv_table_base[2][2][2])[2];
    int16_t (*p_mv_table)[2];            ///< MV table (1MV per MB) p-frame encoding
    int16_t (*b_forw_mv_table)[2];       ///< MV table (1MV per MB) forward mode b-frame encoding
    int16_t (*b_back_mv_table)[2];       ///< MV table (1MV per MB) backward mode b-frame encoding
    int16_t (*b_bidir_forw_mv_table)[2]; ///< MV table (1MV per MB) bidir mode b-frame encoding
    int16_t (*b_bidir_back_mv_table)[2]; ///< MV table (1MV per MB) bidir mode b-frame encoding
    int16_t (*b_direct_mv_table)[2];     ///< MV table (1MV per MB) direct mode b-frame encoding
    int16_t (*p_field_mv_table[2][2])[2];   ///< MV table (2MV per MB) interlaced p-frame encoding
    int16_t (*b_field_mv_table[2][2][2])[2];///< MV table (4MV per MB) interlaced b-frame encoding
    uint8_t (*p_field_select_table[2]);
    uint8_t (*b_field_select_table[2][2]);
    int me_method;                       ///< ME algorithm
    int mv_dir;
#define MV_DIR_FORWARD   1
#define MV_DIR_BACKWARD  2
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
    int16_t direct_scale_mv[2][64];   ///< precomputed to avoid divisions in ff_mpeg4_set_direct_mv

    MotionEstContext me;

    int no_rounding;  /**< apply no rounding to motion compensation (MPEG4, msmpeg4, ...)
                        for b-frames rounding mode is always 0 */

    /* macroblock layer */
    int mb_x, mb_y;
    int mb_skip_run;
    int mb_intra;
    uint16_t *mb_type;  ///< Table for candidate MB types for encoding (defines in mpegutils.h)

    int block_index[6]; ///< index to current MB in block based arrays with edges
    int block_wrap[6];
    uint8_t *dest[3];

    int *mb_index2xy;        ///< mb_index -> mb_x + mb_y*mb_stride

    /** matrix transmitted in the bitstream */
    uint16_t intra_matrix[64];
    uint16_t chroma_intra_matrix[64];
    uint16_t inter_matrix[64];
    uint16_t chroma_inter_matrix[64];

    int intra_quant_bias;    ///< bias for the quantizer
    int inter_quant_bias;    ///< bias for the quantizer
    int min_qcoeff;          ///< minimum encodable coefficient
    int max_qcoeff;          ///< maximum encodable coefficient
    int ac_esc_length;       ///< num of bits needed to encode the longest esc
    uint8_t *intra_ac_vlc_length;
    uint8_t *intra_ac_vlc_last_length;
    uint8_t *intra_chroma_ac_vlc_length;
    uint8_t *intra_chroma_ac_vlc_last_length;
    uint8_t *inter_ac_vlc_length;
    uint8_t *inter_ac_vlc_last_length;
    uint8_t *luma_dc_vlc_length;
#define UNI_AC_ENC_INDEX(run,level) ((run)*128 + (level))

    int coded_score[12];

    /** precomputed matrix (combine qscale and DCT renorm) */
    int (*q_intra_matrix)[64];
    int (*q_chroma_intra_matrix)[64];
    int (*q_inter_matrix)[64];
    /** identical to the above but for MMX & these are not permutated, second 64 entries are bias*/
    uint16_t (*q_intra_matrix16)[2][64];
    uint16_t (*q_chroma_intra_matrix16)[2][64];
    uint16_t (*q_inter_matrix16)[2][64];

    /* noise reduction */
    int (*dct_error_sum)[64];
    int dct_count[2];
    uint16_t (*dct_offset)[64];

    /* bit rate control */
    int64_t total_bits;
    int frame_bits;                ///< bits used for the current frame
    int stuffing_bits;             ///< bits used for stuffing
    int next_lambda;               ///< next lambda used for retrying to encode a frame
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
    int resync_mb_x;                 ///< x position of last resync marker
    int resync_mb_y;                 ///< y position of last resync marker
    GetBitContext last_resync_gb;    ///< used to search for the next resync marker
    int mb_num_left;                 ///< number of MBs left in this video packet (for partitioned Slices only)
    int next_p_frame_damaged;        ///< set if the next p frame is damaged, to avoid showing trashed b frames

    ParseContext parse_context;

    /* H.263 specific */
    int gob_index;
    int obmc;                       ///< overlapped block motion compensation
    int mb_info;                    ///< interval for outputting info about mb offsets as side data
    int prev_mb_info, last_mb_info;
    uint8_t *mb_info_ptr;
    int mb_info_size;
    int ehc_mode;

    /* H.263+ specific */
    int umvplus;                    ///< == H263+ && unrestricted_mv
    int h263_aic_dir;               ///< AIC direction: 0 = left, 1 = top
    int h263_slice_structured;
    int alt_inter_vlc;              ///< alternative inter vlc
    int modified_quant;
    int loop_filter;
    int custom_pcf;

    /* mpeg4 specific */
    ///< number of bits to represent the fractional part of time (encoder only)
    int time_increment_bits;
    int last_time_base;
    int time_base;                  ///< time in seconds of last I,P,S Frame
    int64_t time;                   ///< time of current frame
    int64_t last_non_b_time;
    uint16_t pp_time;               ///< time distance between the last 2 p,s,i frames
    uint16_t pb_time;               ///< time distance between the last b and p,s,i frame
    uint16_t pp_field_time;
    uint16_t pb_field_time;         ///< like above, just for interlaced
    int real_sprite_warping_points;
    int sprite_offset[2][2];         ///< sprite offset[isChroma][isMVY]
    int sprite_delta[2][2];          ///< sprite_delta [isY][isMVY]
    int mcsel;
    int quant_precision;
    int quarter_sample;              ///< 1->qpel, 0->half pel ME/MC
    int aspect_ratio_info; //FIXME remove
    int sprite_warping_accuracy;
    int data_partitioning;           ///< data partitioning flag from header
    int partitioned_frame;           ///< is current frame partitioned
    int low_delay;                   ///< no reordering needed / has no b-frames
    int vo_type;
    PutBitContext tex_pb;            ///< used for data partitioned VOPs
    PutBitContext pb2;               ///< used for data partitioned VOPs
    int mpeg_quant;
    int padding_bug_score;             ///< used to detect the VERY common padding bug in MPEG4

    /* divx specific, used to workaround (many) bugs in divx5 */
    int divx_packed;
    uint8_t *bitstream_buffer; //Divx 5.01 puts several frames in a single one, this is used to reorder them
    int bitstream_buffer_size;
    unsigned int allocated_bitstream_buffer_size;

    /* RV10 specific */
    int rv10_version; ///< RV10 version: 0 or 3
    int rv10_first_dc_coded[3];

    /* MJPEG specific */
    struct MJpegContext *mjpeg_ctx;
    int esc_pos;

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
    int gop_picture_number;  ///< index of the first picture of a GOP based on fake_pic_num & mpeg1 specific
    int last_mv_dir;         ///< last mv_dir, used for b frame encoding
    uint8_t *vbv_delay_ptr;  ///< pointer to vbv_delay in the bitstream

    /* MPEG-2-specific - I wished not to have to support this mess. */
    int progressive_sequence;
    int mpeg_f_code[2][2];

    // picture structure defines are loaded from mpegutils.h
    int picture_structure;

    int intra_dc_precision;
    int frame_pred_frame_dct;
    int top_field_first;
    int concealment_motion_vectors;
    int q_scale_type;
    int intra_vlc_format;
    int alternate_scan;
    int seq_disp_ext;
    int repeat_first_field;
    int chroma_420_type;
    int chroma_format;
#define CHROMA_420 1
#define CHROMA_422 2
#define CHROMA_444 3
    int chroma_x_shift;//depend on pix_format, that depend on chroma_format
    int chroma_y_shift;

    int progressive_frame;
    int full_pel[2];
    int interlaced_dct;
    int first_field;         ///< is 1 for the first field of a field picture 0 otherwise
    int drop_frame_timecode; ///< timecode is in drop frame format.
    int scan_offset;         ///< reserve space for SVCD scan offset user data.

    /* RTP specific */
    int rtp_mode;

    char *tc_opt_str;        ///< timecode option string
    AVTimecode tc;           ///< timecode context

    uint8_t *ptr_lastgob;
    int swap_uv;             //vcr2 codec is an MPEG-2 variant with U and V swapped
    int pack_pblocks;        //xvmc needs to keep blocks without gaps.
    int16_t (*pblocks[12])[64];

    int16_t (*block)[64]; ///< points to one of the following blocks
    int16_t (*blocks)[12][64]; // for HQ mode we need to keep the best block
    int (*decode_mb)(struct MpegEncContext *s, int16_t block[6][64]); // used by some codecs to avoid a switch()
#define SLICE_OK         0
#define SLICE_ERROR     -1
#define SLICE_END       -2 ///<end marker found
#define SLICE_NOEND     -3 ///<no end marker or error found but mb count exceeded

    void (*dct_unquantize_mpeg1_intra)(struct MpegEncContext *s,
                           int16_t *block/*align 16*/, int n, int qscale);
    void (*dct_unquantize_mpeg1_inter)(struct MpegEncContext *s,
                           int16_t *block/*align 16*/, int n, int qscale);
    void (*dct_unquantize_mpeg2_intra)(struct MpegEncContext *s,
                           int16_t *block/*align 16*/, int n, int qscale);
    void (*dct_unquantize_mpeg2_inter)(struct MpegEncContext *s,
                           int16_t *block/*align 16*/, int n, int qscale);
    void (*dct_unquantize_h263_intra)(struct MpegEncContext *s,
                           int16_t *block/*align 16*/, int n, int qscale);
    void (*dct_unquantize_h263_inter)(struct MpegEncContext *s,
                           int16_t *block/*align 16*/, int n, int qscale);
    void (*dct_unquantize_intra)(struct MpegEncContext *s, // unquantizer to use (mpeg4 can use both)
                           int16_t *block/*align 16*/, int n, int qscale);
    void (*dct_unquantize_inter)(struct MpegEncContext *s, // unquantizer to use (mpeg4 can use both)
                           int16_t *block/*align 16*/, int n, int qscale);
    int (*dct_quantize)(struct MpegEncContext *s, int16_t *block/*align 16*/, int n, int qscale, int *overflow);
    int (*fast_dct_quantize)(struct MpegEncContext *s, int16_t *block/*align 16*/, int n, int qscale, int *overflow);
    void (*denoise_dct)(struct MpegEncContext *s, int16_t *block);

    int mpv_flags;      ///< flags set by private options
    int quantizer_noise_shaping;

    /**
     * ratecontrol qmin qmax limiting method
     * 0-> clipping, 1-> use a nice continuous function to limit qscale within qmin/qmax.
     */
    float rc_qsquish;
    float rc_qmod_amp;
    int   rc_qmod_freq;
    float rc_initial_cplx;
    float rc_buffer_aggressivity;
    float border_masking;
    int lmin, lmax;

    char *rc_eq;

    /* temp buffers for rate control */
    float *cplx_tab, *bits_tab;

    /* flag to indicate a reinitialization is required, e.g. after
     * a frame size change */
    int context_reinit;

    ERContext er;

    int error_rate;

    /* temporary frames used by b_frame_strategy = 2 */
    AVFrame *tmp_frames[MAX_B_FRAMES + 2];
} MpegEncContext;

/* mpegvideo_enc common options */
#define FF_MPV_FLAG_SKIP_RD      0x0001
#define FF_MPV_FLAG_STRICT_GOP   0x0002
#define FF_MPV_FLAG_QP_RD        0x0004
#define FF_MPV_FLAG_CBP_RD       0x0008
#define FF_MPV_FLAG_NAQ          0x0010
#define FF_MPV_FLAG_MV0          0x0020

#ifndef FF_MPV_OFFSET
#define FF_MPV_OFFSET(x) offsetof(MpegEncContext, x)
#endif
#define FF_MPV_OPT_FLAGS (AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM)
#define FF_MPV_COMMON_OPTS \
{ "mpv_flags",      "Flags common for all mpegvideo-based encoders.", FF_MPV_OFFSET(mpv_flags), AV_OPT_TYPE_FLAGS, { .i64 = 0 }, INT_MIN, INT_MAX, FF_MPV_OPT_FLAGS, "mpv_flags" },\
{ "skip_rd",        "RD optimal MB level residual skipping", 0, AV_OPT_TYPE_CONST, { .i64 = FF_MPV_FLAG_SKIP_RD },    0, 0, FF_MPV_OPT_FLAGS, "mpv_flags" },\
{ "strict_gop",     "Strictly enforce gop size",             0, AV_OPT_TYPE_CONST, { .i64 = FF_MPV_FLAG_STRICT_GOP }, 0, 0, FF_MPV_OPT_FLAGS, "mpv_flags" },\
{ "qp_rd",          "Use rate distortion optimization for qp selection", 0, AV_OPT_TYPE_CONST, { .i64 = FF_MPV_FLAG_QP_RD },  0, 0, FF_MPV_OPT_FLAGS, "mpv_flags" },\
{ "cbp_rd",         "use rate distortion optimization for CBP",          0, AV_OPT_TYPE_CONST, { .i64 = FF_MPV_FLAG_CBP_RD }, 0, 0, FF_MPV_OPT_FLAGS, "mpv_flags" },\
{ "naq",            "normalize adaptive quantization",                   0, AV_OPT_TYPE_CONST, { .i64 = FF_MPV_FLAG_NAQ },    0, 0, FF_MPV_OPT_FLAGS, "mpv_flags" },\
{ "mv0",            "always try a mb with mv=<0,0>",                     0, AV_OPT_TYPE_CONST, { .i64 = FF_MPV_FLAG_MV0 },    0, 0, FF_MPV_OPT_FLAGS, "mpv_flags" },\
{ "luma_elim_threshold",   "single coefficient elimination threshold for luminance (negative values also consider dc coefficient)",\
                                                                      FF_MPV_OFFSET(luma_elim_threshold), AV_OPT_TYPE_INT, { .i64 = 0 }, INT_MIN, INT_MAX, FF_MPV_OPT_FLAGS },\
{ "chroma_elim_threshold", "single coefficient elimination threshold for chrominance (negative values also consider dc coefficient)",\
                                                                      FF_MPV_OFFSET(chroma_elim_threshold), AV_OPT_TYPE_INT, { .i64 = 0 }, INT_MIN, INT_MAX, FF_MPV_OPT_FLAGS },\
{ "quantizer_noise_shaping", NULL,                                  FF_MPV_OFFSET(quantizer_noise_shaping), AV_OPT_TYPE_INT, { .i64 = 0 },       0, INT_MAX, FF_MPV_OPT_FLAGS },\
{ "error_rate", "Simulate errors in the bitstream to test error concealment.",                                                                                                  \
                                                                    FF_MPV_OFFSET(error_rate),              AV_OPT_TYPE_INT, { .i64 = 0 },       0, INT_MAX, FF_MPV_OPT_FLAGS },\
{"qsquish", "how to keep quantizer between qmin and qmax (0 = clip, 1 = use differentiable function)",                                                                          \
                                                                    FF_MPV_OFFSET(rc_qsquish), AV_OPT_TYPE_FLOAT, {.dbl = 0 }, 0, 99, FF_MPV_OPT_FLAGS},                        \
{"rc_qmod_amp", "experimental quantizer modulation",                FF_MPV_OFFSET(rc_qmod_amp), AV_OPT_TYPE_FLOAT, {.dbl = 0 }, -FLT_MAX, FLT_MAX, FF_MPV_OPT_FLAGS},           \
{"rc_qmod_freq", "experimental quantizer modulation",               FF_MPV_OFFSET(rc_qmod_freq), AV_OPT_TYPE_INT, {.i64 = 0 }, INT_MIN, INT_MAX, FF_MPV_OPT_FLAGS},             \
{"rc_eq", "Set rate control equation. When computing the expression, besides the standard functions "                                                                           \
          "defined in the section 'Expression Evaluation', the following functions are available: "                                                                             \
          "bits2qp(bits), qp2bits(qp). Also the following constants are available: iTex pTex tex mv "                                                                           \
          "fCode iCount mcVar var isI isP isB avgQP qComp avgIITex avgPITex avgPPTex avgBPTex avgTex.",                                                                         \
                                                                    FF_MPV_OFFSET(rc_eq), AV_OPT_TYPE_STRING,                           .flags = FF_MPV_OPT_FLAGS },            \
{"rc_init_cplx", "initial complexity for 1-pass encoding",          FF_MPV_OFFSET(rc_initial_cplx), AV_OPT_TYPE_FLOAT, {.dbl = 0 }, -FLT_MAX, FLT_MAX, FF_MPV_OPT_FLAGS},       \
{"rc_buf_aggressivity", "currently useless",                        FF_MPV_OFFSET(rc_buffer_aggressivity), AV_OPT_TYPE_FLOAT, {.dbl = 1.0 }, -FLT_MAX, FLT_MAX, FF_MPV_OPT_FLAGS}, \
{"border_mask", "increase the quantizer for macroblocks close to borders", FF_MPV_OFFSET(border_masking), AV_OPT_TYPE_FLOAT, {.dbl = 0 }, -FLT_MAX, FLT_MAX, FF_MPV_OPT_FLAGS},    \
{"lmin", "minimum Lagrange factor (VBR)",                           FF_MPV_OFFSET(lmin), AV_OPT_TYPE_INT, {.i64 =  2*FF_QP2LAMBDA }, 0, INT_MAX, FF_MPV_OPT_FLAGS },            \
{"lmax", "maximum Lagrange factor (VBR)",                           FF_MPV_OFFSET(lmax), AV_OPT_TYPE_INT, {.i64 = 31*FF_QP2LAMBDA }, 0, INT_MAX, FF_MPV_OPT_FLAGS },            \

extern const AVOption ff_mpv_generic_options[];

#define FF_MPV_GENERIC_CLASS(name) \
static const AVClass name ## _class = {\
    .class_name = #name " encoder",\
    .item_name  = av_default_item_name,\
    .option     = ff_mpv_generic_options,\
    .version    = LIBAVUTIL_VERSION_INT,\
};

/**
 * Set the given MpegEncContext to common defaults (same for encoding
 * and decoding).  The changed fields will not depend upon the prior
 * state of the MpegEncContext.
 */
void ff_mpv_common_defaults(MpegEncContext *s);

void ff_dct_encode_init_x86(MpegEncContext *s);

int ff_mpv_common_init(MpegEncContext *s);
void ff_mpv_common_init_arm(MpegEncContext *s);
void ff_mpv_common_init_axp(MpegEncContext *s);
void ff_mpv_common_init_neon(MpegEncContext *s);
void ff_mpv_common_init_ppc(MpegEncContext *s);
void ff_mpv_common_init_x86(MpegEncContext *s);

int ff_mpv_common_frame_size_change(MpegEncContext *s);
void ff_mpv_common_end(MpegEncContext *s);

void ff_mpv_decode_defaults(MpegEncContext *s);
void ff_mpv_decode_init(MpegEncContext *s, AVCodecContext *avctx);
void ff_mpv_decode_mb(MpegEncContext *s, int16_t block[12][64]);
void ff_mpv_report_decode_progress(MpegEncContext *s);

int ff_mpv_frame_start(MpegEncContext *s, AVCodecContext *avctx);
void ff_mpv_frame_end(MpegEncContext *s);

int ff_mpv_lowest_referenced_row(MpegEncContext *s, int dir);

int ff_mpv_encode_init(AVCodecContext *avctx);
void ff_mpv_encode_init_x86(MpegEncContext *s);

int ff_mpv_encode_end(AVCodecContext *avctx);
int ff_mpv_encode_picture(AVCodecContext *avctx, AVPacket *pkt,
                          const AVFrame *frame, int *got_packet);
int ff_mpv_reallocate_putbitbuffer(MpegEncContext *s, size_t threshold, size_t size_increase);

void ff_clean_intra_table_entries(MpegEncContext *s);
void ff_mpeg_draw_horiz_band(MpegEncContext *s, int y, int h);
void ff_mpeg_flush(AVCodecContext *avctx);

void ff_print_debug_info(MpegEncContext *s, Picture *p, AVFrame *pict);
void ff_print_debug_info2(AVCodecContext *avctx, AVFrame *pict, uint8_t *mbskip_table,
                         uint32_t *mbtype_table, int8_t *qscale_table, int16_t (*motion_val[2])[2],
                         int *low_delay,
                         int mb_width, int mb_height, int mb_stride, int quarter_sample);

int ff_mpv_export_qp_table(MpegEncContext *s, AVFrame *f, Picture *p, int qp_type);

void ff_write_quant_matrix(PutBitContext *pb, uint16_t *matrix);

int ff_find_unused_picture(AVCodecContext *avctx, Picture *picture, int shared);
int ff_update_duplicate_context(MpegEncContext *dst, MpegEncContext *src);
int ff_mpeg_update_thread_context(AVCodecContext *dst, const AVCodecContext *src);
void ff_set_qscale(MpegEncContext * s, int qscale);

void ff_mpv_idct_init(MpegEncContext *s);
int ff_dct_encode_init(MpegEncContext *s);
void ff_convert_matrix(MpegEncContext *s, int (*qmat)[64], uint16_t (*qmat16)[2][64],
                       const uint16_t *quant_matrix, int bias, int qmin, int qmax, int intra);
int ff_dct_quantize_c(MpegEncContext *s, int16_t *block, int n, int qscale, int *overflow);

void ff_init_block_index(MpegEncContext *s);

void ff_mpv_motion(MpegEncContext *s,
                   uint8_t *dest_y, uint8_t *dest_cb,
                   uint8_t *dest_cr, int dir,
                   uint8_t **ref_picture,
                   op_pixels_func (*pix_op)[4],
                   qpel_mc_func (*qpix_op)[16]);

/**
 * Allocate a Picture.
 * The pixels are allocated/set by calling get_buffer() if shared = 0.
 */
int ff_alloc_picture(AVCodecContext *avctx, Picture *pic, MotionEstContext *me,
                     ScratchpadContext *sc, int shared, int encoding,
                     int chroma_x_shift, int chroma_y_shift, int out_format,
                     int mb_stride, int mb_width, int mb_height, int b8_stride,
                     ptrdiff_t *linesize, ptrdiff_t *uvlinesize);

int ff_mpeg_framesize_alloc(AVCodecContext *avctx, MotionEstContext *me,
                            ScratchpadContext *sc, int linesize);
/**
 * permute block according to permuatation.
 * @param last last non zero element in scantable order
 */
void ff_block_permute(int16_t *block, uint8_t *permutation, const uint8_t *scantable, int last);

static inline void ff_update_block_index(MpegEncContext *s){
    const int block_size= 8 >> s->avctx->lowres;

    s->block_index[0]+=2;
    s->block_index[1]+=2;
    s->block_index[2]+=2;
    s->block_index[3]+=2;
    s->block_index[4]++;
    s->block_index[5]++;
    s->dest[0]+= 2*block_size;
    s->dest[1]+= block_size;
    s->dest[2]+= block_size;
}

static inline int get_bits_diff(MpegEncContext *s){
    const int bits= put_bits_count(&s->pb);
    const int last= s->last_bits;

    s->last_bits = bits;

    return bits - last;
}

/* rv10.c */
int ff_rv10_encode_picture_header(MpegEncContext *s, int picture_number);
int ff_rv_decode_dc(MpegEncContext *s, int n);
void ff_rv20_encode_picture_header(MpegEncContext *s, int picture_number);

int ff_mpeg_ref_picture(AVCodecContext *avctx, Picture *dst, Picture *src);
void ff_mpeg_unref_picture(AVCodecContext *avctx, Picture *picture);
void ff_free_picture_tables(Picture *pic);


#endif /* AVCODEC_MPEGVIDEO_H */
