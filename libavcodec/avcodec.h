/*
 * copyright (c) 2001 Fabrice Bellard
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef AVCODEC_H
#define AVCODEC_H

/**
 * @file avcodec.h
 * external api header.
 */


#ifdef __cplusplus
extern "C" {
#endif

#include "avutil.h"
#include <sys/types.h> /* size_t */

#define AV_STRINGIFY(s)         AV_TOSTRING(s)
#define AV_TOSTRING(s) #s

#define LIBAVCODEC_VERSION_INT  ((51<<16)+(40<<8)+2)
#define LIBAVCODEC_VERSION      51.40.2
#define LIBAVCODEC_BUILD        LIBAVCODEC_VERSION_INT

#define LIBAVCODEC_IDENT        "Lavc" AV_STRINGIFY(LIBAVCODEC_VERSION)

#define AV_NOPTS_VALUE          INT64_C(0x8000000000000000)
#define AV_TIME_BASE            1000000
#define AV_TIME_BASE_Q          (AVRational){1, AV_TIME_BASE}

/**
 *
 * if you add a codec id to this list add it so that
 * 1. no value of a existing codec id changes (that would break ABI)
 * 2. closest to similar codecs
 */
enum CodecID {
    CODEC_ID_NONE,
    CODEC_ID_MPEG1VIDEO,
    CODEC_ID_MPEG2VIDEO, /* prefered ID for MPEG Video 1 or 2 decoding */
    CODEC_ID_MPEG2VIDEO_XVMC,
    CODEC_ID_H261,
    CODEC_ID_H263,
    CODEC_ID_RV10,
    CODEC_ID_RV20,
    CODEC_ID_MJPEG,
    CODEC_ID_MJPEGB,
    CODEC_ID_LJPEG,
    CODEC_ID_SP5X,
    CODEC_ID_JPEGLS,
    CODEC_ID_MPEG4,
    CODEC_ID_RAWVIDEO,
    CODEC_ID_MSMPEG4V1,
    CODEC_ID_MSMPEG4V2,
    CODEC_ID_MSMPEG4V3,
    CODEC_ID_WMV1,
    CODEC_ID_WMV2,
    CODEC_ID_H263P,
    CODEC_ID_H263I,
    CODEC_ID_FLV1,
    CODEC_ID_SVQ1,
    CODEC_ID_SVQ3,
    CODEC_ID_DVVIDEO,
    CODEC_ID_HUFFYUV,
    CODEC_ID_CYUV,
    CODEC_ID_H264,
    CODEC_ID_INDEO3,
    CODEC_ID_VP3,
    CODEC_ID_THEORA,
    CODEC_ID_ASV1,
    CODEC_ID_ASV2,
    CODEC_ID_FFV1,
    CODEC_ID_4XM,
    CODEC_ID_VCR1,
    CODEC_ID_CLJR,
    CODEC_ID_MDEC,
    CODEC_ID_ROQ,
    CODEC_ID_INTERPLAY_VIDEO,
    CODEC_ID_XAN_WC3,
    CODEC_ID_XAN_WC4,
    CODEC_ID_RPZA,
    CODEC_ID_CINEPAK,
    CODEC_ID_WS_VQA,
    CODEC_ID_MSRLE,
    CODEC_ID_MSVIDEO1,
    CODEC_ID_IDCIN,
    CODEC_ID_8BPS,
    CODEC_ID_SMC,
    CODEC_ID_FLIC,
    CODEC_ID_TRUEMOTION1,
    CODEC_ID_VMDVIDEO,
    CODEC_ID_MSZH,
    CODEC_ID_ZLIB,
    CODEC_ID_QTRLE,
    CODEC_ID_SNOW,
    CODEC_ID_TSCC,
    CODEC_ID_ULTI,
    CODEC_ID_QDRAW,
    CODEC_ID_VIXL,
    CODEC_ID_QPEG,
    CODEC_ID_XVID,
    CODEC_ID_PNG,
    CODEC_ID_PPM,
    CODEC_ID_PBM,
    CODEC_ID_PGM,
    CODEC_ID_PGMYUV,
    CODEC_ID_PAM,
    CODEC_ID_FFVHUFF,
    CODEC_ID_RV30,
    CODEC_ID_RV40,
    CODEC_ID_VC1,
    CODEC_ID_WMV3,
    CODEC_ID_LOCO,
    CODEC_ID_WNV1,
    CODEC_ID_AASC,
    CODEC_ID_INDEO2,
    CODEC_ID_FRAPS,
    CODEC_ID_TRUEMOTION2,
    CODEC_ID_BMP,
    CODEC_ID_CSCD,
    CODEC_ID_MMVIDEO,
    CODEC_ID_ZMBV,
    CODEC_ID_AVS,
    CODEC_ID_SMACKVIDEO,
    CODEC_ID_NUV,
    CODEC_ID_KMVC,
    CODEC_ID_FLASHSV,
    CODEC_ID_CAVS,
    CODEC_ID_JPEG2000,
    CODEC_ID_VMNC,
    CODEC_ID_VP5,
    CODEC_ID_VP6,
    CODEC_ID_VP6F,
    CODEC_ID_TARGA,
    CODEC_ID_DSICINVIDEO,
    CODEC_ID_TIERTEXSEQVIDEO,
    CODEC_ID_TIFF,
    CODEC_ID_GIF,
    CODEC_ID_FFH264,
    CODEC_ID_DXA,
    CODEC_ID_DNXHD,
    CODEC_ID_THP,

    /* various pcm "codecs" */
    CODEC_ID_PCM_S16LE= 0x10000,
    CODEC_ID_PCM_S16BE,
    CODEC_ID_PCM_U16LE,
    CODEC_ID_PCM_U16BE,
    CODEC_ID_PCM_S8,
    CODEC_ID_PCM_U8,
    CODEC_ID_PCM_MULAW,
    CODEC_ID_PCM_ALAW,
    CODEC_ID_PCM_S32LE,
    CODEC_ID_PCM_S32BE,
    CODEC_ID_PCM_U32LE,
    CODEC_ID_PCM_U32BE,
    CODEC_ID_PCM_S24LE,
    CODEC_ID_PCM_S24BE,
    CODEC_ID_PCM_U24LE,
    CODEC_ID_PCM_U24BE,
    CODEC_ID_PCM_S24DAUD,

    /* various adpcm codecs */
    CODEC_ID_ADPCM_IMA_QT= 0x11000,
    CODEC_ID_ADPCM_IMA_WAV,
    CODEC_ID_ADPCM_IMA_DK3,
    CODEC_ID_ADPCM_IMA_DK4,
    CODEC_ID_ADPCM_IMA_WS,
    CODEC_ID_ADPCM_IMA_SMJPEG,
    CODEC_ID_ADPCM_MS,
    CODEC_ID_ADPCM_4XM,
    CODEC_ID_ADPCM_XA,
    CODEC_ID_ADPCM_ADX,
    CODEC_ID_ADPCM_EA,
    CODEC_ID_ADPCM_G726,
    CODEC_ID_ADPCM_CT,
    CODEC_ID_ADPCM_SWF,
    CODEC_ID_ADPCM_YAMAHA,
    CODEC_ID_ADPCM_SBPRO_4,
    CODEC_ID_ADPCM_SBPRO_3,
    CODEC_ID_ADPCM_SBPRO_2,

    /* AMR */
    CODEC_ID_AMR_NB= 0x12000,
    CODEC_ID_AMR_WB,

    /* RealAudio codecs*/
    CODEC_ID_RA_144= 0x13000,
    CODEC_ID_RA_288,

    /* various DPCM codecs */
    CODEC_ID_ROQ_DPCM= 0x14000,
    CODEC_ID_INTERPLAY_DPCM,
    CODEC_ID_XAN_DPCM,
    CODEC_ID_SOL_DPCM,

    CODEC_ID_MP2= 0x15000,
    CODEC_ID_MP3, /* prefered ID for MPEG Audio layer 1, 2 or3 decoding */
    CODEC_ID_AAC,
#if LIBAVCODEC_VERSION_INT < ((52<<16)+(0<<8)+0)
    CODEC_ID_MPEG4AAC,
#endif
    CODEC_ID_AC3,
    CODEC_ID_DTS,
    CODEC_ID_VORBIS,
    CODEC_ID_DVAUDIO,
    CODEC_ID_WMAV1,
    CODEC_ID_WMAV2,
    CODEC_ID_MACE3,
    CODEC_ID_MACE6,
    CODEC_ID_VMDAUDIO,
    CODEC_ID_SONIC,
    CODEC_ID_SONIC_LS,
    CODEC_ID_FLAC,
    CODEC_ID_MP3ADU,
    CODEC_ID_MP3ON4,
    CODEC_ID_SHORTEN,
    CODEC_ID_ALAC,
    CODEC_ID_WESTWOOD_SND1,
    CODEC_ID_GSM, /* As in Berlin toast format */
    CODEC_ID_QDM2,
    CODEC_ID_COOK,
    CODEC_ID_TRUESPEECH,
    CODEC_ID_TTA,
    CODEC_ID_SMACKAUDIO,
    CODEC_ID_QCELP,
    CODEC_ID_WAVPACK,
    CODEC_ID_DSICINAUDIO,
    CODEC_ID_IMC,
    CODEC_ID_MUSEPACK7,
    CODEC_ID_MLP,
    CODEC_ID_GSM_MS, /* As found in WAV */

    /* subtitle codecs */
    CODEC_ID_DVD_SUBTITLE= 0x17000,
    CODEC_ID_DVB_SUBTITLE,

    CODEC_ID_MPEG2TS= 0x20000, /* _FAKE_ codec to indicate a raw MPEG2 transport
                         stream (only used by libavformat) */
};

#if LIBAVCODEC_VERSION_INT < ((52<<16)+(0<<8)+0)
/* CODEC_ID_MP3LAME is absolete */
#define CODEC_ID_MP3LAME CODEC_ID_MP3
#define CODEC_ID_MPEG4AAC CODEC_ID_AAC
#endif

enum CodecType {
    CODEC_TYPE_UNKNOWN = -1,
    CODEC_TYPE_VIDEO,
    CODEC_TYPE_AUDIO,
    CODEC_TYPE_DATA,
    CODEC_TYPE_SUBTITLE,
    CODEC_TYPE_NB
};

/* currently unused, may be used if 24/32 bits samples ever supported */
/* all in native endian */
enum SampleFormat {
    SAMPLE_FMT_NONE = -1,
    SAMPLE_FMT_U8,              ///< unsigned 8 bits
    SAMPLE_FMT_S16,             ///< signed 16 bits
    SAMPLE_FMT_S24,             ///< signed 24 bits
    SAMPLE_FMT_S32,             ///< signed 32 bits
    SAMPLE_FMT_FLT,             ///< float
};

/* in bytes */
#define AVCODEC_MAX_AUDIO_FRAME_SIZE 192000 // 1 second of 48khz 32bit audio

/**
 * Required number of additionally allocated bytes at the end of the input bitstream for decoding.
 * this is mainly needed because some optimized bitstream readers read
 * 32 or 64 bit at once and could read over the end<br>
 * Note, if the first 23 bits of the additional bytes are not 0 then damaged
 * MPEG bitstreams could cause overread and segfault
 */
#define FF_INPUT_BUFFER_PADDING_SIZE 8

/**
 * minimum encoding buffer size.
 * used to avoid some checks during header writing
 */
#define FF_MIN_BUFFER_SIZE 16384

/* motion estimation type, EPZS by default */
enum Motion_Est_ID {
    ME_ZERO = 1,
    ME_FULL,
    ME_LOG,
    ME_PHODS,
    ME_EPZS,
    ME_X1,
    ME_HEX,
    ME_UMH,
    ME_ITER,
};

enum AVDiscard{
//we leave some space between them for extensions (drop some keyframes for intra only or drop just some bidir frames)
    AVDISCARD_NONE   =-16, ///< discard nothing
    AVDISCARD_DEFAULT=  0, ///< discard useless packets like 0 size packets in avi
    AVDISCARD_NONREF =  8, ///< discard all non reference
    AVDISCARD_BIDIR  = 16, ///< discard all bidirectional frames
    AVDISCARD_NONKEY = 32, ///< discard all frames except keyframes
    AVDISCARD_ALL    = 48, ///< discard all
};

typedef struct RcOverride{
    int start_frame;
    int end_frame;
    int qscale; // if this is 0 then quality_factor will be used instead
    float quality_factor;
} RcOverride;

#define FF_MAX_B_FRAMES 16

/* encoding support
   these flags can be passed in AVCodecContext.flags before initing
   Note: not everything is supported yet.
*/

#define CODEC_FLAG_QSCALE 0x0002  ///< use fixed qscale
#define CODEC_FLAG_4MV    0x0004  ///< 4 MV per MB allowed / Advanced prediction for H263
#define CODEC_FLAG_QPEL   0x0010  ///< use qpel MC
#define CODEC_FLAG_GMC    0x0020  ///< use GMC
#define CODEC_FLAG_MV0    0x0040  ///< always try a MB with MV=<0,0>
#define CODEC_FLAG_PART   0x0080  ///< use data partitioning
/* parent program guarantees that the input for b-frame containing streams is not written to
   for at least s->max_b_frames+1 frames, if this is not set than the input will be copied */
#define CODEC_FLAG_INPUT_PRESERVED 0x0100
#define CODEC_FLAG_PASS1 0x0200   ///< use internal 2pass ratecontrol in first  pass mode
#define CODEC_FLAG_PASS2 0x0400   ///< use internal 2pass ratecontrol in second pass mode
#define CODEC_FLAG_EXTERN_HUFF 0x1000 ///< use external huffman table (for mjpeg)
#define CODEC_FLAG_GRAY  0x2000   ///< only decode/encode grayscale
#define CODEC_FLAG_EMU_EDGE 0x4000///< don't draw edges
#define CODEC_FLAG_PSNR           0x8000 ///< error[?] variables will be set during encoding
#define CODEC_FLAG_TRUNCATED  0x00010000 /** input bitstream might be truncated at a random location instead
                                            of only at frame boundaries */
#define CODEC_FLAG_NORMALIZE_AQP  0x00020000 ///< normalize adaptive quantization
#define CODEC_FLAG_INTERLACED_DCT 0x00040000 ///< use interlaced dct
#define CODEC_FLAG_LOW_DELAY      0x00080000 ///< force low delay
#define CODEC_FLAG_ALT_SCAN       0x00100000 ///< use alternate scan
#define CODEC_FLAG_TRELLIS_QUANT  0x00200000 ///< use trellis quantization
#define CODEC_FLAG_GLOBAL_HEADER  0x00400000 ///< place global headers in extradata instead of every keyframe
#define CODEC_FLAG_BITEXACT       0x00800000 ///< use only bitexact stuff (except (i)dct)
/* Fx : Flag for h263+ extra options */
#if LIBAVCODEC_VERSION_INT < ((52<<16)+(0<<8)+0)
#define CODEC_FLAG_H263P_AIC      0x01000000 ///< H263 Advanced intra coding / MPEG4 AC prediction (remove this)
#endif
#define CODEC_FLAG_AC_PRED        0x01000000 ///< H263 Advanced intra coding / MPEG4 AC prediction
#define CODEC_FLAG_H263P_UMV      0x02000000 ///< Unlimited motion vector
#define CODEC_FLAG_CBP_RD         0x04000000 ///< use rate distortion optimization for cbp
#define CODEC_FLAG_QP_RD          0x08000000 ///< use rate distortion optimization for qp selectioon
#define CODEC_FLAG_H263P_AIV      0x00000008 ///< H263 Alternative inter vlc
#define CODEC_FLAG_OBMC           0x00000001 ///< OBMC
#define CODEC_FLAG_LOOP_FILTER    0x00000800 ///< loop filter
#define CODEC_FLAG_H263P_SLICE_STRUCT 0x10000000
#define CODEC_FLAG_INTERLACED_ME  0x20000000 ///< interlaced motion estimation
#define CODEC_FLAG_SVCD_SCAN_OFFSET 0x40000000 ///< will reserve space for SVCD scan offset user data
#define CODEC_FLAG_CLOSED_GOP     ((int)0x80000000)
#define CODEC_FLAG2_FAST          0x00000001 ///< allow non spec compliant speedup tricks
#define CODEC_FLAG2_STRICT_GOP    0x00000002 ///< strictly enforce GOP size
#define CODEC_FLAG2_NO_OUTPUT     0x00000004 ///< skip bitstream encoding
#define CODEC_FLAG2_LOCAL_HEADER  0x00000008 ///< place global headers at every keyframe instead of in extradata
#define CODEC_FLAG2_BPYRAMID      0x00000010 ///< H.264 allow b-frames to be used as references
#define CODEC_FLAG2_WPRED         0x00000020 ///< H.264 weighted biprediction for b-frames
#define CODEC_FLAG2_MIXED_REFS    0x00000040 ///< H.264 one reference per partition, as opposed to one reference per macroblock
#define CODEC_FLAG2_8X8DCT        0x00000080 ///< H.264 high profile 8x8 transform
#define CODEC_FLAG2_FASTPSKIP     0x00000100 ///< H.264 fast pskip
#define CODEC_FLAG2_AUD           0x00000200 ///< H.264 access unit delimiters
#define CODEC_FLAG2_BRDO          0x00000400 ///< b-frame rate-distortion optimization
#define CODEC_FLAG2_INTRA_VLC     0x00000800 ///< use MPEG-2 intra VLC table
#define CODEC_FLAG2_MEMC_ONLY     0x00001000 ///< only do ME/MC (I frames -> ref, P frame -> ME+MC)
#define CODEC_FLAG2_DROP_FRAME_TIMECODE 0x00002000 ///< timecode is in drop frame format
#define CODEC_FLAG2_SKIP_RD       0x00004000 ///< RD optimal MB level residual skiping
#define CODEC_FLAG2_CHUNKS        0x00008000 ///< input bitstream might be truncated at a packet boundaries instead of only at frame boundaries
#define CODEC_FLAG2_NON_LINEAR_QUANT 0x00010000 ///< use MPEG-2 non linear quantizer

/* Unsupported options :
 *              Syntax Arithmetic coding (SAC)
 *              Reference Picture Selection
 *              Independent Segment Decoding */
/* /Fx */
/* codec capabilities */

#define CODEC_CAP_DRAW_HORIZ_BAND 0x0001 ///< decoder can use draw_horiz_band callback
/**
 * Codec uses get_buffer() for allocating buffers.
 * direct rendering method 1
 */
#define CODEC_CAP_DR1             0x0002
/* if 'parse_only' field is true, then avcodec_parse_frame() can be
   used */
#define CODEC_CAP_PARSE_ONLY      0x0004
#define CODEC_CAP_TRUNCATED       0x0008
/* codec can export data for HW decoding (XvMC) */
#define CODEC_CAP_HWACCEL         0x0010
/**
 * codec has a non zero delay and needs to be feeded with NULL at the end to get the delayed data.
 * if this is not set, the codec is guaranteed to never be feeded with NULL data
 */
#define CODEC_CAP_DELAY           0x0020
/**
 * Codec can be fed a final frame with a smaller size.
 * This can be used to prevent truncation of the last audio samples.
 */
#define CODEC_CAP_SMALL_LAST_FRAME 0x0040

//the following defines may change, don't expect compatibility if you use them
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
#define MB_TYPE_GMC        0x0400
#define MB_TYPE_SKIP       0x0800
#define MB_TYPE_P0L0       0x1000
#define MB_TYPE_P1L0       0x2000
#define MB_TYPE_P0L1       0x4000
#define MB_TYPE_P1L1       0x8000
#define MB_TYPE_L0         (MB_TYPE_P0L0 | MB_TYPE_P1L0)
#define MB_TYPE_L1         (MB_TYPE_P0L1 | MB_TYPE_P1L1)
#define MB_TYPE_L0L1       (MB_TYPE_L0   | MB_TYPE_L1)
#define MB_TYPE_QUANT      0x00010000
#define MB_TYPE_CBP        0x00020000
//Note bits 24-31 are reserved for codec specific use (h264 ref0, mpeg1 0mv, ...)

/**
 * Pan Scan area.
 * this specifies the area which should be displayed. Note there may be multiple such areas for one frame
 */
typedef struct AVPanScan{
    /**
     * id.
     * - encoding: set by user.
     * - decoding: set by lavc
     */
    int id;

    /**
     * width and height in 1/16 pel
     * - encoding: set by user.
     * - decoding: set by lavc
     */
    int width;
    int height;

    /**
     * position of the top left corner in 1/16 pel for up to 3 fields/frames.
     * - encoding: set by user.
     * - decoding: set by lavc
     */
    int16_t position[3][2];
}AVPanScan;

#define FF_COMMON_FRAME \
    /**\
     * pointer to the picture planes.\
     * this might be different from the first allocated byte\
     * - encoding: \
     * - decoding: \
     */\
    uint8_t *data[4];\
    int linesize[4];\
    /**\
     * pointer to the first allocated byte of the picture. can be used in get_buffer/release_buffer\
     * this isn't used by lavc unless the default get/release_buffer() is used\
     * - encoding: \
     * - decoding: \
     */\
    uint8_t *base[4];\
    /**\
     * 1 -> keyframe, 0-> not\
     * - encoding: set by lavc\
     * - decoding: set by lavc\
     */\
    int key_frame;\
\
    /**\
     * picture type of the frame, see ?_TYPE below.\
     * - encoding: set by lavc for coded_picture (and set by user for input)\
     * - decoding: set by lavc\
     */\
    int pict_type;\
\
    /**\
     * presentation timestamp in time_base units (time when frame should be shown to user)\
     * if AV_NOPTS_VALUE then frame_rate = 1/time_base will be assumed\
     * - encoding: MUST be set by user\
     * - decoding: set by lavc\
     */\
    int64_t pts;\
\
    /**\
     * picture number in bitstream order.\
     * - encoding: set by\
     * - decoding: set by lavc\
     */\
    int coded_picture_number;\
    /**\
     * picture number in display order.\
     * - encoding: set by\
     * - decoding: set by lavc\
     */\
    int display_picture_number;\
\
    /**\
     * quality (between 1 (good) and FF_LAMBDA_MAX (bad)) \
     * - encoding: set by lavc for coded_picture (and set by user for input)\
     * - decoding: set by lavc\
     */\
    int quality; \
\
    /**\
     * buffer age (1->was last buffer and dint change, 2->..., ...).\
     * set to INT_MAX if the buffer has not been used yet \
     * - encoding: unused\
     * - decoding: MUST be set by get_buffer()\
     */\
    int age;\
\
    /**\
     * is this picture used as reference\
     * - encoding: unused\
     * - decoding: set by lavc (before get_buffer() call))\
     */\
    int reference;\
\
    /**\
     * QP table\
     * - encoding: unused\
     * - decoding: set by lavc\
     */\
    int8_t *qscale_table;\
    /**\
     * QP store stride\
     * - encoding: unused\
     * - decoding: set by lavc\
     */\
    int qstride;\
\
    /**\
     * mbskip_table[mb]>=1 if MB didnt change\
     * stride= mb_width = (width+15)>>4\
     * - encoding: unused\
     * - decoding: set by lavc\
     */\
    uint8_t *mbskip_table;\
\
    /**\
     * Motion vector table.\
     * @code\
     * example:\
     * int mv_sample_log2= 4 - motion_subsample_log2;\
     * int mb_width= (width+15)>>4;\
     * int mv_stride= (mb_width << mv_sample_log2) + 1;\
     * motion_val[direction][x + y*mv_stride][0->mv_x, 1->mv_y];\
     * @endcode\
     * - encoding: set by user\
     * - decoding: set by lavc\
     */\
    int16_t (*motion_val[2])[2];\
\
    /**\
     * Macroblock type table\
     * mb_type_base + mb_width + 2\
     * - encoding: set by user\
     * - decoding: set by lavc\
     */\
    uint32_t *mb_type;\
\
    /**\
     * log2 of the size of the block which a single vector in motion_val represents: \
     * (4->16x16, 3->8x8, 2-> 4x4, 1-> 2x2)\
     * - encoding: unused\
     * - decoding: set by lavc\
     */\
    uint8_t motion_subsample_log2;\
\
    /**\
     * for some private data of the user\
     * - encoding: unused\
     * - decoding: set by user\
     */\
    void *opaque;\
\
    /**\
     * error\
     * - encoding: set by lavc if flags&CODEC_FLAG_PSNR\
     * - decoding: unused\
     */\
    uint64_t error[4];\
\
    /**\
     * type of the buffer (to keep track of who has to dealloc data[*])\
     * - encoding: set by the one who allocs it\
     * - decoding: set by the one who allocs it\
     * Note: user allocated (direct rendering) & internal buffers can not coexist currently\
     */\
    int type;\
    \
    /**\
     * when decoding, this signal how much the picture must be delayed.\
     * extra_delay = repeat_pict / (2*fps)\
     * - encoding: unused\
     * - decoding: set by lavc\
     */\
    int repeat_pict;\
    \
    /**\
     * \
     */\
    int qscale_type;\
    \
    /**\
     * The content of the picture is interlaced.\
     * - encoding: set by user\
     * - decoding: set by lavc (default 0)\
     */\
    int interlaced_frame;\
    \
    /**\
     * if the content is interlaced, is top field displayed first.\
     * - encoding: set by user\
     * - decoding: set by lavc\
     */\
    int top_field_first;\
    \
    /**\
     * Pan scan.\
     * - encoding: set by user\
     * - decoding: set by lavc\
     */\
    AVPanScan *pan_scan;\
    \
    /**\
     * tell user application that palette has changed from previous frame.\
     * - encoding: ??? (no palette-enabled encoder yet)\
     * - decoding: set by lavc (default 0)\
     */\
    int palette_has_changed;\
    \
    /**\
     * Codec suggestion on buffer type if != 0\
     * - encoding: unused\
     * - decoding: set by lavc (before get_buffer() call))\
     */\
    int buffer_hints;\
\
    /**\
     * DCT coeffitients\
     * - encoding: unused\
     * - decoding: set by lavc\
     */\
    short *dct_coeff;\
\
    /**\
     * Motion referece frame index\
     * - encoding: set by user\
     * - decoding: set by lavc\
     */\
    int8_t *ref_index[2];

#define FF_QSCALE_TYPE_MPEG1 0
#define FF_QSCALE_TYPE_MPEG2 1
#define FF_QSCALE_TYPE_H264  2

#define FF_BUFFER_TYPE_INTERNAL 1
#define FF_BUFFER_TYPE_USER     2 ///< Direct rendering buffers (image is (de)allocated by user)
#define FF_BUFFER_TYPE_SHARED   4 ///< buffer from somewhere else, don't dealloc image (data/base), all other tables are not shared
#define FF_BUFFER_TYPE_COPY     8 ///< just a (modified) copy of some other buffer, don't dealloc anything


#define FF_I_TYPE 1 // Intra
#define FF_P_TYPE 2 // Predicted
#define FF_B_TYPE 3 // Bi-dir predicted
#define FF_S_TYPE 4 // S(GMC)-VOP MPEG4
#define FF_SI_TYPE 5
#define FF_SP_TYPE 6

#define FF_BUFFER_HINTS_VALID    0x01 // Buffer hints value is meaningful (if 0 ignore)
#define FF_BUFFER_HINTS_READABLE 0x02 // Codec will read from buffer
#define FF_BUFFER_HINTS_PRESERVE 0x04 // User must not alter buffer content
#define FF_BUFFER_HINTS_REUSABLE 0x08 // Codec will reuse the buffer (update)

/**
 * Audio Video Frame.
 */
typedef struct AVFrame {
    FF_COMMON_FRAME
} AVFrame;

#define DEFAULT_FRAME_RATE_BASE 1001000

/**
 * main external api structure.
 */
typedef struct AVCodecContext {
    /**
     * Info on struct for av_log
     * - set by avcodec_alloc_context
     */
    AVClass *av_class;
    /**
     * the average bitrate.
     * - encoding: set by user. unused for constant quantizer encoding
     * - decoding: set by lavc. 0 or some bitrate if this info is available in the stream
     */
    int bit_rate;

    /**
     * number of bits the bitstream is allowed to diverge from the reference.
     *           the reference can be CBR (for CBR pass1) or VBR (for pass2)
     * - encoding: set by user. unused for constant quantizer encoding
     * - decoding: unused
     */
    int bit_rate_tolerance;

    /**
     * CODEC_FLAG_*.
     * - encoding: set by user.
     * - decoding: set by user.
     */
    int flags;

    /**
     * some codecs needs additionnal format info. It is stored here
     * if any muxer uses this then ALL demuxers/parsers AND encoders for the specific codec MUST set it correctly
     * too otherwise stream copy breaks
     * in general use of this field by muxers is not recommanded
     * - encoding: set by lavc.
     * - decoding: set by lavc. (FIXME is this ok?)
     */
    int sub_id;

    /**
     * motion estimation algorithm used for video coding.
     * 1 (zero), 2 (full), 3 (log), 4 (phods), 5 (epzs), 6 (x1), 7 (hex),
     * 8 (umh), 9 (iter) [7, 8 are x264 specific, 9 is snow specific]
     * - encoding: MUST be set by user.
     * - decoding: unused
     */
    int me_method;

    /**
     * some codecs need / can use extra-data like huffman tables.
     * mjpeg: huffman tables
     * rv10: additional flags
     * mpeg4: global headers (they can be in the bitstream or here)
     * the allocated memory should be FF_INPUT_BUFFER_PADDING_SIZE bytes larger
     * then extradata_size to avoid prolems if its read with the bitstream reader
     * the bytewise contents of extradata must not depend on the architecture or cpu endianness
     * - encoding: set/allocated/freed by lavc.
     * - decoding: set/allocated/freed by user.
     */
    uint8_t *extradata;
    int extradata_size;

    /**
     * this is the fundamental unit of time (in seconds) in terms
     * of which frame timestamps are represented. for fixed-fps content,
     * timebase should be 1/framerate and timestamp increments should be
     * identically 1.
     * - encoding: MUST be set by user
     * - decoding: set by lavc.
     */
    AVRational time_base;

    /* video only */
    /**
     * picture width / height.
     * - encoding: MUST be set by user.
     * - decoding: set by lavc.
     * Note, for compatibility its possible to set this instead of
     * coded_width/height before decoding
     */
    int width, height;

#define FF_ASPECT_EXTENDED 15

    /**
     * the number of pictures in a group of pitures, or 0 for intra_only.
     * - encoding: set by user.
     * - decoding: unused
     */
    int gop_size;

    /**
     * pixel format, see PIX_FMT_xxx.
     * - encoding: set by user.
     * - decoding: set by lavc.
     */
    enum PixelFormat pix_fmt;

    /**
     * Frame rate emulation. If not zero lower layer (i.e. format handler)
     * has to read frames at native frame rate.
     * - encoding: set by user.
     * - decoding: unused.
     */
    int rate_emu;

    /**
     * if non NULL, 'draw_horiz_band' is called by the libavcodec
     * decoder to draw an horizontal band. It improve cache usage. Not
     * all codecs can do that. You must check the codec capabilities
     * before
     * - encoding: unused
     * - decoding: set by user.
     * @param height the height of the slice
     * @param y the y position of the slice
     * @param type 1->top field, 2->bottom field, 3->frame
     * @param offset offset into the AVFrame.data from which the slice should be read
     */
    void (*draw_horiz_band)(struct AVCodecContext *s,
                            const AVFrame *src, int offset[4],
                            int y, int type, int height);

    /* audio only */
    int sample_rate; ///< samples per sec
    int channels;

    /**
     * audio sample format.
     * - encoding: set by user.
     * - decoding: set by lavc.
     */
    enum SampleFormat sample_fmt;  ///< sample format, currenly unused

    /* the following data should not be initialized */
    /**
     * samples per packet. initialized when calling 'init'
     */
    int frame_size;
    int frame_number;   ///< audio or video frame number
    int real_pict_num;  ///< returns the real picture number of previous encoded frame

    /**
     * number of frames the decoded output will be delayed relative to
     * the encoded input.
     * - encoding: set by lavc.
     * - decoding: unused
     */
    int delay;

    /* - encoding parameters */
    float qcompress;  ///< amount of qscale change between easy & hard scenes (0.0-1.0)
    float qblur;      ///< amount of qscale smoothing over time (0.0-1.0)

    /**
     * minimum quantizer.
     * - encoding: set by user.
     * - decoding: unused
     */
    int qmin;

    /**
     * maximum quantizer.
     * - encoding: set by user.
     * - decoding: unused
     */
    int qmax;

    /**
     * maximum quantizer difference between frames.
     * - encoding: set by user.
     * - decoding: unused
     */
    int max_qdiff;

    /**
     * maximum number of b frames between non b frames.
     * note: the output will be delayed by max_b_frames+1 relative to the input
     * - encoding: set by user.
     * - decoding: unused
     */
    int max_b_frames;

    /**
     * qscale factor between ip and b frames.
     * - encoding: set by user.
     * - decoding: unused
     */
    float b_quant_factor;

    /** obsolete FIXME remove */
    int rc_strategy;
#define FF_RC_STRATEGY_XVID 1

    int b_frame_strategy;

    /**
     * hurry up amount.
     * - encoding: unused
     * - decoding: set by user. 1-> skip b frames, 2-> skip idct/dequant too, 5-> skip everything except header
     * @deprecated Deprecated in favor of skip_idct and skip_frame.
     */
    int hurry_up;

    struct AVCodec *codec;

    void *priv_data;

#if LIBAVCODEC_VERSION_INT < ((52<<16)+(0<<8)+0)
    /* unused, FIXME remove*/
    int rtp_mode;
#endif

    int rtp_payload_size;   /* The size of the RTP payload: the coder will  */
                            /* do it's best to deliver a chunk with size    */
                            /* below rtp_payload_size, the chunk will start */
                            /* with a start code on some codecs like H.263  */
                            /* This doesn't take account of any particular  */
                            /* headers inside the transmited RTP payload    */


    /* The RTP callback: This function is called   */
    /* every time the encoder has a packet to send */
    /* Depends on the encoder if the data starts   */
    /* with a Start Code (it should) H.263 does.   */
    /* mb_nb contains the number of macroblocks    */
    /* encoded in the RTP payload                  */
    void (*rtp_callback)(struct AVCodecContext *avctx, void *data, int size, int mb_nb);

    /* statistics, used for 2-pass encoding */
    int mv_bits;
    int header_bits;
    int i_tex_bits;
    int p_tex_bits;
    int i_count;
    int p_count;
    int skip_count;
    int misc_bits;

    /**
     * number of bits used for the previously encoded frame.
     * - encoding: set by lavc
     * - decoding: unused
     */
    int frame_bits;

    /**
     * private data of the user, can be used to carry app specific stuff.
     * - encoding: set by user
     * - decoding: set by user
     */
    void *opaque;

    char codec_name[32];
    enum CodecType codec_type; /* see CODEC_TYPE_xxx */
    enum CodecID codec_id; /* see CODEC_ID_xxx */

    /**
     * fourcc (LSB first, so "ABCD" -> ('D'<<24) + ('C'<<16) + ('B'<<8) + 'A').
     * this is used to workaround some encoder bugs
     * a demuxer should set this to what is stored in the field used to identify the codec
     * if there are mutiple such fields in a container then the demuxer should choose the one
     * which maximizes the information about the used codec
     * if the codec tag field in a container is larger then 32bit then the demxuer should
     * remap the longer id to 32bit with a table or other structure alternatively a new
     * extra_codec_tag + size could be added but for this a clear advantage must be demonstrated
     * first
     * - encoding: set by user, if not then the default based on codec_id will be used
     * - decoding: set by user, will be converted to upper case by lavc during init
     */
    unsigned int codec_tag;

    /**
     * workaround bugs in encoders which sometimes cannot be detected automatically.
     * - encoding: set by user
     * - decoding: set by user
     */
    int workaround_bugs;
#define FF_BUG_AUTODETECT       1  ///< autodetection
#define FF_BUG_OLD_MSMPEG4      2
#define FF_BUG_XVID_ILACE       4
#define FF_BUG_UMP4             8
#define FF_BUG_NO_PADDING       16
#define FF_BUG_AMV              32
#define FF_BUG_AC_VLC           0  ///< will be removed, libavcodec can now handle these non compliant files by default
#define FF_BUG_QPEL_CHROMA      64
#define FF_BUG_STD_QPEL         128
#define FF_BUG_QPEL_CHROMA2     256
#define FF_BUG_DIRECT_BLOCKSIZE 512
#define FF_BUG_EDGE             1024
#define FF_BUG_HPEL_CHROMA      2048
#define FF_BUG_DC_CLIP          4096
#define FF_BUG_MS               8192 ///< workaround various bugs in microsofts broken decoders
//#define FF_BUG_FAKE_SCALABILITY 16 //autodetection should work 100%

    /**
     * luma single coeff elimination threshold.
     * - encoding: set by user
     * - decoding: unused
     */
    int luma_elim_threshold;

    /**
     * chroma single coeff elimination threshold.
     * - encoding: set by user
     * - decoding: unused
     */
    int chroma_elim_threshold;

    /**
     * strictly follow the std (MPEG4, ...).
     * - encoding: set by user
     * - decoding: unused
     */
    int strict_std_compliance;
#define FF_COMPLIANCE_VERY_STRICT   2 ///< strictly conform to a older more strict version of the spec or reference software
#define FF_COMPLIANCE_STRICT        1 ///< strictly conform to all the things in the spec no matter what consequences
#define FF_COMPLIANCE_NORMAL        0
#define FF_COMPLIANCE_INOFFICIAL   -1 ///< allow inofficial extensions
#define FF_COMPLIANCE_EXPERIMENTAL -2 ///< allow non standarized experimental things

    /**
     * qscale offset between ip and b frames.
     * if > 0 then the last p frame quantizer will be used (q= lastp_q*factor+offset)
     * if < 0 then normal ratecontrol will be done (q= -normal_q*factor+offset)
     * - encoding: set by user.
     * - decoding: unused
     */
    float b_quant_offset;

    /**
     * error resilience higher values will detect more errors but may missdetect
     * some more or less valid parts as errors.
     * - encoding: unused
     * - decoding: set by user
     */
    int error_resilience;
#define FF_ER_CAREFUL         1
#define FF_ER_COMPLIANT       2
#define FF_ER_AGGRESSIVE      3
#define FF_ER_VERY_AGGRESSIVE 4

    /**
     * called at the beginning of each frame to get a buffer for it.
     * if pic.reference is set then the frame will be read later by lavc
     * avcodec_align_dimensions() should be used to find the required width and
     * height, as they normally need to be rounded up to the next multiple of 16
     * - encoding: unused
     * - decoding: set by lavc, user can override
     */
    int (*get_buffer)(struct AVCodecContext *c, AVFrame *pic);

    /**
     * called to release buffers which where allocated with get_buffer.
     * a released buffer can be reused in get_buffer()
     * pic.data[*] must be set to NULL
     * - encoding: unused
     * - decoding: set by lavc, user can override
     */
    void (*release_buffer)(struct AVCodecContext *c, AVFrame *pic);

    /**
     * if 1 the stream has a 1 frame delay during decoding.
     * - encoding: set by lavc
     * - decoding: set by lavc
     */
    int has_b_frames;

    /**
     * number of bytes per packet if constant and known or 0
     * used by some WAV based audio codecs
     */
    int block_align;

    int parse_only; /* - decoding only: if true, only parsing is done
                       (function avcodec_parse_frame()). The frame
                       data is returned. Only MPEG codecs support this now. */

    /**
     * 0-> h263 quant 1-> mpeg quant.
     * - encoding: set by user.
     * - decoding: unused
     */
    int mpeg_quant;

    /**
     * pass1 encoding statistics output buffer.
     * - encoding: set by lavc
     * - decoding: unused
     */
    char *stats_out;

    /**
     * pass2 encoding statistics input buffer.
     * concatenated stuff from stats_out of pass1 should be placed here
     * - encoding: allocated/set/freed by user
     * - decoding: unused
     */
    char *stats_in;

    /**
     * ratecontrol qmin qmax limiting method.
     * 0-> clipping, 1-> use a nice continous function to limit qscale wthin qmin/qmax
     * - encoding: set by user.
     * - decoding: unused
     */
    float rc_qsquish;

    float rc_qmod_amp;
    int rc_qmod_freq;

    /**
     * ratecontrol override, see RcOverride.
     * - encoding: allocated/set/freed by user.
     * - decoding: unused
     */
    RcOverride *rc_override;
    int rc_override_count;

    /**
     * rate control equation.
     * - encoding: set by user
     * - decoding: unused
     */
    char *rc_eq;

    /**
     * maximum bitrate.
     * - encoding: set by user.
     * - decoding: unused
     */
    int rc_max_rate;

    /**
     * minimum bitrate.
     * - encoding: set by user.
     * - decoding: unused
     */
    int rc_min_rate;

    /**
     * decoder bitstream buffer size.
     * - encoding: set by user.
     * - decoding: unused
     */
    int rc_buffer_size;
    float rc_buffer_aggressivity;

    /**
     * qscale factor between p and i frames.
     * if > 0 then the last p frame quantizer will be used (q= lastp_q*factor+offset)
     * if < 0 then normal ratecontrol will be done (q= -normal_q*factor+offset)
     * - encoding: set by user.
     * - decoding: unused
     */
    float i_quant_factor;

    /**
     * qscale offset between p and i frames.
     * - encoding: set by user.
     * - decoding: unused
     */
    float i_quant_offset;

    /**
     * initial complexity for pass1 ratecontrol.
     * - encoding: set by user.
     * - decoding: unused
     */
    float rc_initial_cplx;

    /**
     * dct algorithm, see FF_DCT_* below.
     * - encoding: set by user
     * - decoding: unused
     */
    int dct_algo;
#define FF_DCT_AUTO    0
#define FF_DCT_FASTINT 1
#define FF_DCT_INT     2
#define FF_DCT_MMX     3
#define FF_DCT_MLIB    4
#define FF_DCT_ALTIVEC 5
#define FF_DCT_FAAN    6

    /**
     * luminance masking (0-> disabled).
     * - encoding: set by user
     * - decoding: unused
     */
    float lumi_masking;

    /**
     * temporary complexity masking (0-> disabled).
     * - encoding: set by user
     * - decoding: unused
     */
    float temporal_cplx_masking;

    /**
     * spatial complexity masking (0-> disabled).
     * - encoding: set by user
     * - decoding: unused
     */
    float spatial_cplx_masking;

    /**
     * p block masking (0-> disabled).
     * - encoding: set by user
     * - decoding: unused
     */
    float p_masking;

    /**
     * darkness masking (0-> disabled).
     * - encoding: set by user
     * - decoding: unused
     */
    float dark_masking;


    /* for binary compatibility */
    int unused;

    /**
     * idct algorithm, see FF_IDCT_* below.
     * - encoding: set by user
     * - decoding: set by user
     */
    int idct_algo;
#define FF_IDCT_AUTO         0
#define FF_IDCT_INT          1
#define FF_IDCT_SIMPLE       2
#define FF_IDCT_SIMPLEMMX    3
#define FF_IDCT_LIBMPEG2MMX  4
#define FF_IDCT_PS2          5
#define FF_IDCT_MLIB         6
#define FF_IDCT_ARM          7
#define FF_IDCT_ALTIVEC      8
#define FF_IDCT_SH4          9
#define FF_IDCT_SIMPLEARM    10
#define FF_IDCT_H264         11
#define FF_IDCT_VP3          12
#define FF_IDCT_IPP          13
#define FF_IDCT_XVIDMMX      14
#define FF_IDCT_CAVS         15
#define FF_IDCT_SIMPLEARMV5TE 16
#define FF_IDCT_SIMPLEARMV6  17

    /**
     * slice count.
     * - encoding: set by lavc
     * - decoding: set by user (or 0)
     */
    int slice_count;
    /**
     * slice offsets in the frame in bytes.
     * - encoding: set/allocated by lavc
     * - decoding: set/allocated by user (or NULL)
     */
    int *slice_offset;

    /**
     * error concealment flags.
     * - encoding: unused
     * - decoding: set by user
     */
    int error_concealment;
#define FF_EC_GUESS_MVS   1
#define FF_EC_DEBLOCK     2

    /**
     * dsp_mask could be add used to disable unwanted CPU features
     * CPU features (i.e. MMX, SSE. ...)
     *
     * with FORCE flag you may instead enable given CPU features
     * (Dangerous: usable in case of misdetection, improper usage however will
     * result into program crash)
     */
    unsigned dsp_mask;
#define FF_MM_FORCE    0x80000000 /* force usage of selected flags (OR) */
    /* lower 16 bits - CPU features */
#ifdef HAVE_MMX
#define FF_MM_MMX      0x0001 /* standard MMX */
#define FF_MM_3DNOW    0x0004 /* AMD 3DNOW */
#define FF_MM_MMXEXT   0x0002 /* SSE integer functions or AMD MMX ext */
#define FF_MM_SSE      0x0008 /* SSE functions */
#define FF_MM_SSE2     0x0010 /* PIV SSE2 functions */
#define FF_MM_3DNOWEXT 0x0020 /* AMD 3DNowExt */
#endif /* HAVE_MMX */
#ifdef HAVE_IWMMXT
#define FF_MM_IWMMXT   0x0100 /* XScale IWMMXT */
#endif /* HAVE_IWMMXT */

    /**
     * bits per sample/pixel from the demuxer (needed for huffyuv).
     * - encoding: set by lavc
     * - decoding: set by user
     */
     int bits_per_sample;

    /**
     * prediction method (needed for huffyuv).
     * - encoding: set by user
     * - decoding: unused
     */
     int prediction_method;
#define FF_PRED_LEFT   0
#define FF_PRED_PLANE  1
#define FF_PRED_MEDIAN 2

    /**
     * sample aspect ratio (0 if unknown).
     * numerator and denominator must be relative prime and smaller then 256 for some video standards
     * - encoding: set by user.
     * - decoding: set by lavc.
     */
    AVRational sample_aspect_ratio;

    /**
     * the picture in the bitstream.
     * - encoding: set by lavc
     * - decoding: set by lavc
     */
    AVFrame *coded_frame;

    /**
     * debug.
     * - encoding: set by user.
     * - decoding: set by user.
     */
    int debug;
#define FF_DEBUG_PICT_INFO 1
#define FF_DEBUG_RC        2
#define FF_DEBUG_BITSTREAM 4
#define FF_DEBUG_MB_TYPE   8
#define FF_DEBUG_QP        16
#define FF_DEBUG_MV        32
#define FF_DEBUG_DCT_COEFF 0x00000040
#define FF_DEBUG_SKIP      0x00000080
#define FF_DEBUG_STARTCODE 0x00000100
#define FF_DEBUG_PTS       0x00000200
#define FF_DEBUG_ER        0x00000400
#define FF_DEBUG_MMCO      0x00000800
#define FF_DEBUG_BUGS      0x00001000
#define FF_DEBUG_VIS_QP    0x00002000
#define FF_DEBUG_VIS_MB_TYPE 0x00004000

    /**
     * debug.
     * - encoding: set by user.
     * - decoding: set by user.
     */
    int debug_mv;
#define FF_DEBUG_VIS_MV_P_FOR  0x00000001 //visualize forward predicted MVs of P frames
#define FF_DEBUG_VIS_MV_B_FOR  0x00000002 //visualize forward predicted MVs of B frames
#define FF_DEBUG_VIS_MV_B_BACK 0x00000004 //visualize backward predicted MVs of B frames

    /**
     * error.
     * - encoding: set by lavc if flags&CODEC_FLAG_PSNR
     * - decoding: unused
     */
    uint64_t error[4];

    /**
     * minimum MB quantizer.
     * - encoding: unused
     * - decoding: unused
     */
    int mb_qmin;

    /**
     * maximum MB quantizer.
     * - encoding: unused
     * - decoding: unused
     */
    int mb_qmax;

    /**
     * motion estimation compare function.
     * - encoding: set by user.
     * - decoding: unused
     */
    int me_cmp;
    /**
     * subpixel motion estimation compare function.
     * - encoding: set by user.
     * - decoding: unused
     */
    int me_sub_cmp;
    /**
     * macroblock compare function (not supported yet).
     * - encoding: set by user.
     * - decoding: unused
     */
    int mb_cmp;
    /**
     * interlaced dct compare function
     * - encoding: set by user.
     * - decoding: unused
     */
    int ildct_cmp;
#define FF_CMP_SAD  0
#define FF_CMP_SSE  1
#define FF_CMP_SATD 2
#define FF_CMP_DCT  3
#define FF_CMP_PSNR 4
#define FF_CMP_BIT  5
#define FF_CMP_RD   6
#define FF_CMP_ZERO 7
#define FF_CMP_VSAD 8
#define FF_CMP_VSSE 9
#define FF_CMP_NSSE 10
#define FF_CMP_W53  11
#define FF_CMP_W97  12
#define FF_CMP_DCTMAX 13
#define FF_CMP_DCT264 14
#define FF_CMP_CHROMA 256

    /**
     * ME diamond size & shape.
     * - encoding: set by user.
     * - decoding: unused
     */
    int dia_size;

    /**
     * amount of previous MV predictors (2a+1 x 2a+1 square).
     * - encoding: set by user.
     * - decoding: unused
     */
    int last_predictor_count;

    /**
     * pre pass for motion estimation.
     * - encoding: set by user.
     * - decoding: unused
     */
    int pre_me;

    /**
     * motion estimation pre pass compare function.
     * - encoding: set by user.
     * - decoding: unused
     */
    int me_pre_cmp;

    /**
     * ME pre pass diamond size & shape.
     * - encoding: set by user.
     * - decoding: unused
     */
    int pre_dia_size;

    /**
     * subpel ME quality.
     * - encoding: set by user.
     * - decoding: unused
     */
    int me_subpel_quality;

    /**
     * callback to negotiate the pixelFormat.
     * @param fmt is the list of formats which are supported by the codec,
     * its terminated by -1 as 0 is a valid format, the formats are ordered by quality
     * the first is allways the native one
     * @return the choosen format
     * - encoding: unused
     * - decoding: set by user, if not set then the native format will always be choosen
     */
    enum PixelFormat (*get_format)(struct AVCodecContext *s, const enum PixelFormat * fmt);

    /**
     * DTG active format information (additionnal aspect ratio
     * information only used in DVB MPEG2 transport streams). 0 if
     * not set.
     *
     * - encoding: unused.
     * - decoding: set by decoder
     */
    int dtg_active_format;
#define FF_DTG_AFD_SAME         8
#define FF_DTG_AFD_4_3          9
#define FF_DTG_AFD_16_9         10
#define FF_DTG_AFD_14_9         11
#define FF_DTG_AFD_4_3_SP_14_9  13
#define FF_DTG_AFD_16_9_SP_14_9 14
#define FF_DTG_AFD_SP_4_3       15

    /**
     * Maximum motion estimation search range in subpel units.
     * if 0 then no limit
     *
     * - encoding: set by user.
     * - decoding: unused.
     */
    int me_range;

    /**
     * intra quantizer bias.
     * - encoding: set by user.
     * - decoding: unused
     */
    int intra_quant_bias;
#define FF_DEFAULT_QUANT_BIAS 999999

    /**
     * inter quantizer bias.
     * - encoding: set by user.
     * - decoding: unused
     */
    int inter_quant_bias;

    /**
     * color table ID.
     * - encoding: unused.
     * - decoding: which clrtable should be used for 8bit RGB images
     *             table have to be stored somewhere FIXME
     */
    int color_table_id;

    /**
     * internal_buffer count.
     * Don't touch, used by lavc default_get_buffer()
     */
    int internal_buffer_count;

    /**
     * internal_buffers.
     * Don't touch, used by lavc default_get_buffer()
     */
    void *internal_buffer;

#define FF_LAMBDA_SHIFT 7
#define FF_LAMBDA_SCALE (1<<FF_LAMBDA_SHIFT)
#define FF_QP2LAMBDA 118 ///< factor to convert from H.263 QP to lambda
#define FF_LAMBDA_MAX (256*128-1)

#define FF_QUALITY_SCALE FF_LAMBDA_SCALE //FIXME maybe remove
    /**
     * global quality for codecs which cannot change it per frame.
     * this should be proportional to MPEG1/2/4 qscale.
     * - encoding: set by user.
     * - decoding: unused
     */
    int global_quality;

#define FF_CODER_TYPE_VLC       0
#define FF_CODER_TYPE_AC        1
#define FF_CODER_TYPE_RAW       2
#define FF_CODER_TYPE_RLE       3
#define FF_CODER_TYPE_DEFLATE   4
    /**
     * coder type
     * - encoding: set by user.
     * - decoding: unused
     */
    int coder_type;

    /**
     * context model
     * - encoding: set by user.
     * - decoding: unused
     */
    int context_model;
#if 0
    /**
     *
     * - encoding: unused
     * - decoding: set by user.
     */
    uint8_t * (*realloc)(struct AVCodecContext *s, uint8_t *buf, int buf_size);
#endif

    /**
     * slice flags
     * - encoding: unused
     * - decoding: set by user.
     */
    int slice_flags;
#define SLICE_FLAG_CODED_ORDER    0x0001 ///< draw_horiz_band() is called in coded order instead of display
#define SLICE_FLAG_ALLOW_FIELD    0x0002 ///< allow draw_horiz_band() with field slices (MPEG2 field pics)
#define SLICE_FLAG_ALLOW_PLANE    0x0004 ///< allow draw_horiz_band() with 1 component at a time (SVQ1)

    /**
     * XVideo Motion Acceleration
     * - encoding: forbidden
     * - decoding: set by decoder
     */
    int xvmc_acceleration;

    /**
     * macroblock decision mode
     * - encoding: set by user.
     * - decoding: unused
     */
    int mb_decision;
#define FF_MB_DECISION_SIMPLE 0        ///< uses mb_cmp
#define FF_MB_DECISION_BITS   1        ///< chooses the one which needs the fewest bits
#define FF_MB_DECISION_RD     2        ///< rate distoration

    /**
     * custom intra quantization matrix
     * - encoding: set by user, can be NULL
     * - decoding: set by lavc
     */
    uint16_t *intra_matrix;

    /**
     * custom inter quantization matrix
     * - encoding: set by user, can be NULL
     * - decoding: set by lavc
     */
    uint16_t *inter_matrix;

    /**
     * fourcc from the AVI stream header (LSB first, so "ABCD" -> ('D'<<24) + ('C'<<16) + ('B'<<8) + 'A').
     * this is used to workaround some encoder bugs
     * - encoding: unused
     * - decoding: set by user, will be converted to upper case by lavc during init
     */
    unsigned int stream_codec_tag;

    /**
     * scene change detection threshold.
     * 0 is default, larger means fewer detected scene changes
     * - encoding: set by user.
     * - decoding: unused
     */
    int scenechange_threshold;

    /**
     * minimum lagrange multipler
     * - encoding: set by user.
     * - decoding: unused
     */
    int lmin;

    /**
     * maximum lagrange multipler
     * - encoding: set by user.
     * - decoding: unused
     */
    int lmax;

    /**
     * Palette control structure
     * - encoding: ??? (no palette-enabled encoder yet)
     * - decoding: set by user.
     */
    struct AVPaletteControl *palctrl;

    /**
     * noise reduction strength
     * - encoding: set by user.
     * - decoding: unused
     */
    int noise_reduction;

    /**
     * called at the beginning of a frame to get cr buffer for it.
     * buffer type (size, hints) must be the same. lavc won't check it.
     * lavc will pass previous buffer in pic, function should return
     * same buffer or new buffer with old frame "painted" into it.
     * if pic.data[0] == NULL must behave like get_buffer().
     * - encoding: unused
     * - decoding: set by lavc, user can override
     */
    int (*reget_buffer)(struct AVCodecContext *c, AVFrame *pic);

    /**
     * number of bits which should be loaded into the rc buffer before decoding starts
     * - encoding: set by user.
     * - decoding: unused
     */
    int rc_initial_buffer_occupancy;

    /**
     *
     * - encoding: set by user.
     * - decoding: unused
     */
    int inter_threshold;

    /**
     * CODEC_FLAG2_*.
     * - encoding: set by user.
     * - decoding: set by user.
     */
    int flags2;

    /**
     * simulates errors in the bitstream to test error concealment.
     * - encoding: set by user.
     * - decoding: unused.
     */
    int error_rate;

    /**
     * MP3 antialias algorithm, see FF_AA_* below.
     * - encoding: unused
     * - decoding: set by user
     */
    int antialias_algo;
#define FF_AA_AUTO    0
#define FF_AA_FASTINT 1 //not implemented yet
#define FF_AA_INT     2
#define FF_AA_FLOAT   3
    /**
     * Quantizer noise shaping.
     * - encoding: set by user
     * - decoding: unused
     */
    int quantizer_noise_shaping;

    /**
     * Thread count.
     * is used to decide how many independent tasks should be passed to execute()
     * - encoding: set by user
     * - decoding: set by user
     */
    int thread_count;

    /**
     * the codec may call this to execute several independent things. it will return only after
     * finishing all tasks, the user may replace this with some multithreaded implementation, the
     * default implementation will execute the parts serially
     * @param count the number of things to execute
     * - encoding: set by lavc, user can override
     * - decoding: set by lavc, user can override
     */
    int (*execute)(struct AVCodecContext *c, int (*func)(struct AVCodecContext *c2, void *arg), void **arg2, int *ret, int count);

    /**
     * Thread opaque.
     * can be used by execute() to store some per AVCodecContext stuff.
     * - encoding: set by execute()
     * - decoding: set by execute()
     */
    void *thread_opaque;

    /**
     * Motion estimation threshold. under which no motion estimation is
     * performed, but instead the user specified motion vectors are used
     *
     * - encoding: set by user
     * - decoding: unused
     */
     int me_threshold;

    /**
     * Macroblock threshold. under which the user specified macroblock types will be used
     * - encoding: set by user
     * - decoding: unused
     */
     int mb_threshold;

    /**
     * precision of the intra dc coefficient - 8.
     * - encoding: set by user
     * - decoding: unused
     */
     int intra_dc_precision;

    /**
     * noise vs. sse weight for the nsse comparsion function.
     * - encoding: set by user
     * - decoding: unused
     */
     int nsse_weight;

    /**
     * number of macroblock rows at the top which are skipped.
     * - encoding: unused
     * - decoding: set by user
     */
     int skip_top;

    /**
     * number of macroblock rows at the bottom which are skipped.
     * - encoding: unused
     * - decoding: set by user
     */
     int skip_bottom;

    /**
     * profile
     * - encoding: set by user
     * - decoding: set by lavc
     */
     int profile;
#define FF_PROFILE_UNKNOWN -99

    /**
     * level
     * - encoding: set by user
     * - decoding: set by lavc
     */
     int level;
#define FF_LEVEL_UNKNOWN -99

    /**
     * low resolution decoding. 1-> 1/2 size, 2->1/4 size
     * - encoding: unused
     * - decoding: set by user
     */
     int lowres;

    /**
     * bitsream width / height. may be different from width/height if lowres
     * or other things are used
     * - encoding: unused
     * - decoding: set by user before init if known, codec should override / dynamically change if needed
     */
    int coded_width, coded_height;

    /**
     * frame skip threshold
     * - encoding: set by user
     * - decoding: unused
     */
    int frame_skip_threshold;

    /**
     * frame skip factor
     * - encoding: set by user
     * - decoding: unused
     */
    int frame_skip_factor;

    /**
     * frame skip exponent
     * - encoding: set by user
     * - decoding: unused
     */
    int frame_skip_exp;

    /**
     * frame skip comparission function
     * - encoding: set by user.
     * - decoding: unused
     */
    int frame_skip_cmp;

    /**
     * border processing masking. raises the quantizer for mbs on the borders
     * of the picture.
     * - encoding: set by user
     * - decoding: unused
     */
    float border_masking;

    /**
     * minimum MB lagrange multipler.
     * - encoding: set by user.
     * - decoding: unused
     */
    int mb_lmin;

    /**
     * maximum MB lagrange multipler.
     * - encoding: set by user.
     * - decoding: unused
     */
    int mb_lmax;

    /**
     *
     * - encoding: set by user.
     * - decoding: unused
     */
    int me_penalty_compensation;

    /**
     *
     * - encoding: unused
     * - decoding: set by user.
     */
    enum AVDiscard skip_loop_filter;

    /**
     *
     * - encoding: unused
     * - decoding: set by user.
     */
    enum AVDiscard skip_idct;

    /**
     *
     * - encoding: unused
     * - decoding: set by user.
     */
    enum AVDiscard skip_frame;

    /**
     *
     * - encoding: set by user.
     * - decoding: unused
     */
    int bidir_refine;

    /**
     *
     * - encoding: set by user.
     * - decoding: unused
     */
    int brd_scale;

    /**
     * constant rate factor - quality-based VBR - values ~correspond to qps
     * - encoding: set by user.
     * - decoding: unused
     */
    float crf;

    /**
     * constant quantization parameter rate control method
     * - encoding: set by user.
     * - decoding: unused
     */
    int cqp;

    /**
     * minimum gop size
     * - encoding: set by user.
     * - decoding: unused
     */
    int keyint_min;

    /**
     * number of reference frames
     * - encoding: set by user.
     * - decoding: unused
     */
    int refs;

    /**
     * chroma qp offset from luma
     * - encoding: set by user.
     * - decoding: unused
     */
    int chromaoffset;

    /**
     * influences how often b-frames are used
     * - encoding: set by user.
     * - decoding: unused
     */
    int bframebias;

    /**
     * trellis RD quantization
     * - encoding: set by user.
     * - decoding: unused
     */
    int trellis;

    /**
     * reduce fluctuations in qp (before curve compression)
     * - encoding: set by user.
     * - decoding: unused
     */
    float complexityblur;

    /**
     * in-loop deblocking filter alphac0 parameter
     * alpha is in the range -6...6
     * - encoding: set by user.
     * - decoding: unused
     */
    int deblockalpha;

    /**
     * in-loop deblocking filter beta parameter
     * beta is in the range -6...6
     * - encoding: set by user.
     * - decoding: unused
     */
    int deblockbeta;

    /**
     * macroblock subpartition sizes to consider - p8x8, p4x4, b8x8, i8x8, i4x4
     * - encoding: set by user.
     * - decoding: unused
     */
    int partitions;
#define X264_PART_I4X4 0x001  /* Analyse i4x4 */
#define X264_PART_I8X8 0x002  /* Analyse i8x8 (requires 8x8 transform) */
#define X264_PART_P8X8 0x010  /* Analyse p16x8, p8x16 and p8x8 */
#define X264_PART_P4X4 0x020  /* Analyse p8x4, p4x8, p4x4 */
#define X264_PART_B8X8 0x100  /* Analyse b16x8, b8x16 and b8x8 */

    /**
     * direct mv prediction mode - 0 (none), 1 (spatial), 2 (temporal)
     * - encoding: set by user.
     * - decoding: unused
     */
    int directpred;

    /**
     * audio cutoff bandwidth (0 means "automatic") . Currently used only by FAAC
     * - encoding: set by user.
     * - decoding: unused
     */
    int cutoff;

    /**
     * multiplied by qscale for each frame and added to scene_change_score
     * - encoding: set by user.
     * - decoding: unused
     */
    int scenechange_factor;

    /**
     *
     * note: value depends upon the compare functin used for fullpel ME
     * - encoding: set by user.
     * - decoding: unused
     */
    int mv0_threshold;

    /**
     * adjusts sensitivity of b_frame_strategy 1
     * - encoding: set by user.
     * - decoding: unused
     */
    int b_sensitivity;

    /**
     * - encoding: set by user.
     * - decoding: unused
     */
    int compression_level;
#define FF_COMPRESSION_DEFAULT -1

    /**
     * sets whether to use LPC mode - used by FLAC encoder
     * - encoding: set by user.
     * - decoding: unused.
     */
    int use_lpc;

    /**
     * LPC coefficient precision - used by FLAC encoder
     * - encoding: set by user.
     * - decoding: unused.
     */
    int lpc_coeff_precision;

    /**
     * - encoding: set by user.
     * - decoding: unused.
     */
    int min_prediction_order;

    /**
     * - encoding: set by user.
     * - decoding: unused.
     */
    int max_prediction_order;

    /**
     * search method for selecting prediction order
     * - encoding: set by user.
     * - decoding: unused.
     */
    int prediction_order_method;

    /**
     * - encoding: set by user.
     * - decoding: unused.
     */
    int min_partition_order;

    /**
     * - encoding: set by user.
     * - decoding: unused.
     */
    int max_partition_order;

    /**
     * GOP timecode frame start number, in non drop frame format
     * - encoding: set by user.
     * - decoding: unused.
     */
    int64_t timecode_frame_start;
} AVCodecContext;

/**
 * AVCodec.
 */
typedef struct AVCodec {
    const char *name;
    enum CodecType type;
    enum CodecID id;
    int priv_data_size;
    int (*init)(AVCodecContext *);
    int (*encode)(AVCodecContext *, uint8_t *buf, int buf_size, void *data);
    int (*close)(AVCodecContext *);
    int (*decode)(AVCodecContext *, void *outdata, int *outdata_size,
                  uint8_t *buf, int buf_size);
    int capabilities;
    struct AVCodec *next;
    void (*flush)(AVCodecContext *);
    const AVRational *supported_framerates; ///array of supported framerates, or NULL if any, array is terminated by {0,0}
    const enum PixelFormat *pix_fmts;       ///array of supported pixel formats, or NULL if unknown, array is terminanted by -1
} AVCodec;

/**
 * four components are given, that's all.
 * the last component is alpha
 */
typedef struct AVPicture {
    uint8_t *data[4];
    int linesize[4];       ///< number of bytes per line
} AVPicture;

/**
 * AVPaletteControl
 * This structure defines a method for communicating palette changes
 * between and demuxer and a decoder.
 * this is totally broken, palette changes should be sent as AVPackets
 */
#define AVPALETTE_SIZE 1024
#define AVPALETTE_COUNT 256
typedef struct AVPaletteControl {

    /* demuxer sets this to 1 to indicate the palette has changed;
     * decoder resets to 0 */
    int palette_changed;

    /* 4-byte ARGB palette entries, stored in native byte order; note that
     * the individual palette components should be on a 8-bit scale; if
     * the palette data comes from a IBM VGA native format, the component
     * data is probably 6 bits in size and needs to be scaled */
    unsigned int palette[AVPALETTE_COUNT];

} AVPaletteControl attribute_deprecated;

typedef struct AVSubtitleRect {
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
    uint16_t nb_colors;
    int linesize;
    uint32_t *rgba_palette;
    uint8_t *bitmap;
} AVSubtitleRect;

typedef struct AVSubtitle {
    uint16_t format; /* 0 = graphics */
    uint32_t start_display_time; /* relative to packet pts, in ms */
    uint32_t end_display_time; /* relative to packet pts, in ms */
    uint32_t num_rects;
    AVSubtitleRect *rects;
} AVSubtitle;

extern AVCodec ac3_encoder;
extern AVCodec amr_nb_encoder;
extern AVCodec amr_wb_encoder;
extern AVCodec asv1_encoder;
extern AVCodec asv2_encoder;
extern AVCodec bmp_encoder;
extern AVCodec dvvideo_encoder;
extern AVCodec faac_encoder;
extern AVCodec ffv1_encoder;
extern AVCodec ffvhuff_encoder;
extern AVCodec flac_encoder;
extern AVCodec flashsv_encoder;
extern AVCodec flv_encoder;
extern AVCodec gif_encoder;
extern AVCodec h261_encoder;
extern AVCodec h263_encoder;
extern AVCodec h263p_encoder;
extern AVCodec h264_encoder;
extern AVCodec huffyuv_encoder;
extern AVCodec jpegls_encoder;
extern AVCodec libgsm_encoder;
extern AVCodec libgsm_ms_encoder;
extern AVCodec libtheora_encoder;
extern AVCodec ljpeg_encoder;
extern AVCodec mdec_encoder;
extern AVCodec mjpeg_encoder;
extern AVCodec mp2_encoder;
extern AVCodec mp3lame_encoder;
extern AVCodec mpeg1video_encoder;
extern AVCodec mpeg2video_encoder;
extern AVCodec mpeg4_encoder;
extern AVCodec msmpeg4v1_encoder;
extern AVCodec msmpeg4v2_encoder;
extern AVCodec msmpeg4v3_encoder;
extern AVCodec oggvorbis_encoder;
extern AVCodec pam_encoder;
extern AVCodec pbm_encoder;
extern AVCodec pgm_encoder;
extern AVCodec pgmyuv_encoder;
extern AVCodec png_encoder;
extern AVCodec ppm_encoder;
extern AVCodec rv10_encoder;
extern AVCodec rv20_encoder;
extern AVCodec snow_encoder;
extern AVCodec sonic_encoder;
extern AVCodec sonic_ls_encoder;
extern AVCodec svq1_encoder;
extern AVCodec targa_encoder;
extern AVCodec tiff_encoder;
extern AVCodec vcr1_encoder;
extern AVCodec vorbis_encoder;
extern AVCodec wmav1_encoder;
extern AVCodec wmav2_encoder;
extern AVCodec wmv1_encoder;
extern AVCodec wmv2_encoder;
extern AVCodec x264_encoder;
extern AVCodec xvid_encoder;
extern AVCodec zlib_encoder;
extern AVCodec zmbv_encoder;

extern AVCodec aac_decoder;
extern AVCodec aasc_decoder;
extern AVCodec alac_decoder;
extern AVCodec amr_nb_decoder;
extern AVCodec amr_wb_decoder;
extern AVCodec asv1_decoder;
extern AVCodec asv2_decoder;
extern AVCodec avs_decoder;
extern AVCodec bmp_decoder;
extern AVCodec cavs_decoder;
extern AVCodec cinepak_decoder;
extern AVCodec cljr_decoder;
extern AVCodec cook_decoder;
extern AVCodec cscd_decoder;
extern AVCodec cyuv_decoder;
extern AVCodec dca_decoder;
extern AVCodec dnxhd_decoder;
extern AVCodec dsicinaudio_decoder;
extern AVCodec dsicinvideo_decoder;
extern AVCodec dvvideo_decoder;
extern AVCodec dxa_decoder;
extern AVCodec eightbps_decoder;
extern AVCodec ffv1_decoder;
extern AVCodec ffvhuff_decoder;
extern AVCodec flac_decoder;
extern AVCodec flashsv_decoder;
extern AVCodec flic_decoder;
extern AVCodec flv_decoder;
extern AVCodec fourxm_decoder;
extern AVCodec fraps_decoder;
extern AVCodec gif_decoder;
extern AVCodec h261_decoder;
extern AVCodec h263_decoder;
extern AVCodec h263i_decoder;
extern AVCodec h264_decoder;
extern AVCodec huffyuv_decoder;
extern AVCodec idcin_decoder;
extern AVCodec imc_decoder;
extern AVCodec indeo2_decoder;
extern AVCodec indeo3_decoder;
extern AVCodec interplay_dpcm_decoder;
extern AVCodec interplay_video_decoder;
extern AVCodec kmvc_decoder;
extern AVCodec libgsm_decoder;
extern AVCodec libgsm_ms_decoder;
extern AVCodec loco_decoder;
extern AVCodec mace3_decoder;
extern AVCodec mace6_decoder;
extern AVCodec mdec_decoder;
extern AVCodec mjpeg_decoder;
extern AVCodec mjpegb_decoder;
extern AVCodec mmvideo_decoder;
extern AVCodec mp2_decoder;
extern AVCodec mp3_decoder;
extern AVCodec mp3adu_decoder;
extern AVCodec mp3on4_decoder;
extern AVCodec mpc7_decoder;
extern AVCodec mpeg1video_decoder;
extern AVCodec mpeg2video_decoder;
extern AVCodec mpeg4_decoder;
extern AVCodec mpeg4aac_decoder;
extern AVCodec mpeg_xvmc_decoder;
extern AVCodec mpegvideo_decoder;
extern AVCodec msmpeg4v1_decoder;
extern AVCodec msmpeg4v2_decoder;
extern AVCodec msmpeg4v3_decoder;
extern AVCodec msrle_decoder;
extern AVCodec msvideo1_decoder;
extern AVCodec mszh_decoder;
extern AVCodec nuv_decoder;
extern AVCodec oggvorbis_decoder;
extern AVCodec png_decoder;
extern AVCodec qdm2_decoder;
extern AVCodec qdraw_decoder;
extern AVCodec qpeg_decoder;
extern AVCodec qtrle_decoder;
extern AVCodec ra_144_decoder;
extern AVCodec ra_288_decoder;
extern AVCodec roq_decoder;
extern AVCodec roq_dpcm_decoder;
extern AVCodec rpza_decoder;
extern AVCodec rv10_decoder;
extern AVCodec rv20_decoder;
extern AVCodec rv30_decoder;
extern AVCodec rv40_decoder;
extern AVCodec shorten_decoder;
extern AVCodec smackaud_decoder;
extern AVCodec smacker_decoder;
extern AVCodec smc_decoder;
extern AVCodec snow_decoder;
extern AVCodec sol_dpcm_decoder;
extern AVCodec sonic_decoder;
extern AVCodec sp5x_decoder;
extern AVCodec svq1_decoder;
extern AVCodec svq3_decoder;
extern AVCodec targa_decoder;
extern AVCodec theora_decoder;
extern AVCodec thp_decoder;
extern AVCodec tiertexseqvideo_decoder;
extern AVCodec tiff_decoder;
extern AVCodec truemotion1_decoder;
extern AVCodec truemotion2_decoder;
extern AVCodec truespeech_decoder;
extern AVCodec tscc_decoder;
extern AVCodec tta_decoder;
extern AVCodec ulti_decoder;
extern AVCodec vc1_decoder;
extern AVCodec vcr1_decoder;
extern AVCodec vmdaudio_decoder;
extern AVCodec vmdvideo_decoder;
extern AVCodec vmnc_decoder;
extern AVCodec vorbis_decoder;
extern AVCodec vp3_decoder;
extern AVCodec vp5_decoder;
extern AVCodec vp6_decoder;
extern AVCodec vp6f_decoder;
extern AVCodec vqa_decoder;
extern AVCodec wavpack_decoder;
extern AVCodec wmav1_decoder;
extern AVCodec wmav2_decoder;
extern AVCodec wmv1_decoder;
extern AVCodec wmv2_decoder;
extern AVCodec wmv3_decoder;
extern AVCodec wnv1_decoder;
extern AVCodec ws_snd1_decoder;
extern AVCodec xan_dpcm_decoder;
extern AVCodec xan_wc3_decoder;
extern AVCodec xl_decoder;
extern AVCodec zlib_decoder;
extern AVCodec zmbv_decoder;

/* pcm codecs */
#define PCM_CODEC(id, name) \
extern AVCodec name ## _decoder; \
extern AVCodec name ## _encoder

PCM_CODEC(CODEC_ID_PCM_ALAW,    pcm_alaw);
PCM_CODEC(CODEC_ID_PCM_MULAW,   pcm_mulaw);
PCM_CODEC(CODEC_ID_PCM_S8,      pcm_s8);
PCM_CODEC(CODEC_ID_PCM_S16BE,   pcm_s16be);
PCM_CODEC(CODEC_ID_PCM_S16LE,   pcm_s16le);
PCM_CODEC(CODEC_ID_PCM_S24BE,   pcm_s24be);
PCM_CODEC(CODEC_ID_PCM_S24DAUD, pcm_s24daud);
PCM_CODEC(CODEC_ID_PCM_S24LE,   pcm_s24le);
PCM_CODEC(CODEC_ID_PCM_S32BE,   pcm_s32be);
PCM_CODEC(CODEC_ID_PCM_S32LE,   pcm_s32le);
PCM_CODEC(CODEC_ID_PCM_U8,      pcm_u8);
PCM_CODEC(CODEC_ID_PCM_U16BE,   pcm_u16be);
PCM_CODEC(CODEC_ID_PCM_U16LE,   pcm_u16le);
PCM_CODEC(CODEC_ID_PCM_U24BE,   pcm_u24be);
PCM_CODEC(CODEC_ID_PCM_U24LE,   pcm_u24le);
PCM_CODEC(CODEC_ID_PCM_U32BE,   pcm_u32be);
PCM_CODEC(CODEC_ID_PCM_U32LE,   pcm_u32le);

/* adpcm codecs */

PCM_CODEC(CODEC_ID_ADPCM_4XM,     adpcm_4xm);
PCM_CODEC(CODEC_ID_ADPCM_ADX,     adpcm_adx);
PCM_CODEC(CODEC_ID_ADPCM_CT,      adpcm_ct);
PCM_CODEC(CODEC_ID_ADPCM_EA,      adpcm_ea);
PCM_CODEC(CODEC_ID_ADPCM_G726,    adpcm_g726);
PCM_CODEC(CODEC_ID_ADPCM_IMA_DK3, adpcm_ima_dk3);
PCM_CODEC(CODEC_ID_ADPCM_IMA_DK4, adpcm_ima_dk4);
PCM_CODEC(CODEC_ID_ADPCM_IMA_QT,  adpcm_ima_qt);
PCM_CODEC(CODEC_ID_ADPCM_IMA_WAV, adpcm_ima_wav);
PCM_CODEC(CODEC_ID_ADPCM_IMA_WS,  adpcm_ima_ws);
PCM_CODEC(CODEC_ID_ADPCM_MS,      adpcm_ms);
PCM_CODEC(CODEC_ID_ADPCM_SBPRO_2, adpcm_sbpro_2);
PCM_CODEC(CODEC_ID_ADPCM_SBPRO_3, adpcm_sbpro_3);
PCM_CODEC(CODEC_ID_ADPCM_SBPRO_4, adpcm_sbpro_4);
PCM_CODEC(CODEC_ID_ADPCM_SMJPEG,  adpcm_ima_smjpeg);
PCM_CODEC(CODEC_ID_ADPCM_SWF,     adpcm_swf);
PCM_CODEC(CODEC_ID_ADPCM_XA,      adpcm_xa);
PCM_CODEC(CODEC_ID_ADPCM_YAMAHA,  adpcm_yamaha);

#undef PCM_CODEC

/* dummy raw video codec */
extern AVCodec rawvideo_decoder;
extern AVCodec rawvideo_encoder;

/* the following codecs use external GPL libs */
extern AVCodec dts_decoder;
extern AVCodec liba52_decoder;

/* subtitles */
extern AVCodec dvbsub_decoder;
extern AVCodec dvbsub_encoder;
extern AVCodec dvdsub_decoder;
extern AVCodec dvdsub_encoder;

/* resample.c */

struct ReSampleContext;
struct AVResampleContext;

typedef struct ReSampleContext ReSampleContext;

ReSampleContext *audio_resample_init(int output_channels, int input_channels,
                                     int output_rate, int input_rate);
int audio_resample(ReSampleContext *s, short *output, short *input, int nb_samples);
void audio_resample_close(ReSampleContext *s);

struct AVResampleContext *av_resample_init(int out_rate, int in_rate, int filter_length, int log2_phase_count, int linear, double cutoff);
int av_resample(struct AVResampleContext *c, short *dst, short *src, int *consumed, int src_size, int dst_size, int update_ctx);
void av_resample_compensate(struct AVResampleContext *c, int sample_delta, int compensation_distance);
void av_resample_close(struct AVResampleContext *c);

#if LIBAVCODEC_VERSION_INT < ((52<<16)+(0<<8)+0)
/* YUV420 format is assumed ! */

struct ImgReSampleContext attribute_deprecated;

typedef struct ImgReSampleContext ImgReSampleContext attribute_deprecated;

attribute_deprecated ImgReSampleContext *img_resample_init(int output_width, int output_height,
                                      int input_width, int input_height);

attribute_deprecated ImgReSampleContext *img_resample_full_init(int owidth, int oheight,
                                      int iwidth, int iheight,
                                      int topBand, int bottomBand,
                                      int leftBand, int rightBand,
                                      int padtop, int padbottom,
                                      int padleft, int padright);


attribute_deprecated void img_resample(ImgReSampleContext *s,
                  AVPicture *output, const AVPicture *input);

attribute_deprecated void img_resample_close(ImgReSampleContext *s);

#endif

/**
 * Allocate memory for a picture.  Call avpicture_free to free it.
 *
 * @param picture the picture to be filled in.
 * @param pix_fmt the format of the picture.
 * @param width the width of the picture.
 * @param height the height of the picture.
 * @return Zero if successful, a negative value if not.
 */
int avpicture_alloc(AVPicture *picture, int pix_fmt, int width, int height);

/**
 * Free a picture previously allocated by avpicture_alloc().
 *
 * @param picture The AVPicture to be freed.
 */
void avpicture_free(AVPicture *picture);

/**
 * Fill in AVPicture's fields.
 * The fields of the given AVPicture are filled in by using the 'ptr' address
 * which points to the image data buffer. Depending on the specified picture
 * format, one or multiple image data pointers and line sizes will be set.
 * If a planar format is specified, several pointers will be set pointing to
 * the different picture planes and the line sizes of the different planes
 * will be stored in the lines_sizes array.
 *
 * @param picture AVPicture who's fields are to be filled in
 * @param ptr Buffer which will contain or contains the actual image data
 * @param pix_fmt The format in which the picture data is stored
 * @param width The width of the image in pixels
 * @param height The height of the image in pixels
 * @return Size of the image data in bytes.
 */
int avpicture_fill(AVPicture *picture, uint8_t *ptr,
                   int pix_fmt, int width, int height);
int avpicture_layout(const AVPicture* src, int pix_fmt, int width, int height,
                     unsigned char *dest, int dest_size);

/**
 * Calculate the size in bytes that a picture of the given width and height
 * would occupy if stored in the given picture format.
 *
 * @param pix_fmt The given picture format
 * @param width The width of the image
 * @param height The height of the image
 * @return Image data size in bytes
 */
int avpicture_get_size(int pix_fmt, int width, int height);
void avcodec_get_chroma_sub_sample(int pix_fmt, int *h_shift, int *v_shift);
const char *avcodec_get_pix_fmt_name(int pix_fmt);
void avcodec_set_dimensions(AVCodecContext *s, int width, int height);
enum PixelFormat avcodec_get_pix_fmt(const char* name);
unsigned int avcodec_pix_fmt_to_codec_tag(enum PixelFormat p);

#define FF_LOSS_RESOLUTION  0x0001 /**< loss due to resolution change */
#define FF_LOSS_DEPTH       0x0002 /**< loss due to color depth change */
#define FF_LOSS_COLORSPACE  0x0004 /**< loss due to color space conversion */
#define FF_LOSS_ALPHA       0x0008 /**< loss of alpha bits */
#define FF_LOSS_COLORQUANT  0x0010 /**< loss due to color quantization */
#define FF_LOSS_CHROMA      0x0020 /**< loss of chroma (e.g. RGB to gray conversion) */

/**
 * Computes what kind of losses will occur when converting from one specific
 * pixel format to another.
 * When converting from one pixel format to another, information loss may occur.
 * For example, when converting from RGB24 to GRAY, the color information will
 * be lost. Similarly, other losses occur when converting from some formats to
 * other formats. These losses can involve loss of chroma, but also loss of
 * resolution, loss of color depth, loss due to the color space conversion, loss
 * of the alpha bits or loss due to color quantization.
 * avcodec_get_fix_fmt_loss() informs you on the various types of losses which
 * will occur when converting from one pixel format to another.
 *
 * @param[in] dst_pix_fmt Destination pixel format.
 * @param[in] src_pix_fmt Source pixel format.
 * @param[in] has_alpha Whether the source pixel format alpha channel is used.
 * @return Combination of flags informing you what kind of losses will occur.
 */
int avcodec_get_pix_fmt_loss(int dst_pix_fmt, int src_pix_fmt,
                             int has_alpha);

/**
 * Finds the best pixel format to convert to given a certain source pixel
 * format.  When converting from one pixel format to another, information loss
 * may occur.  For example, when converting from RGB24 to GRAY, the color
 * information will be lost. Similarly, other losses occur when converting from
 * some formats to other formats. avcodec_find_best_pix_fmt() searches which of
 * the given pixel formats should be used to undergo the least amount of losses.
 * The pixel formats from which it choses one, are determined by the
 * \p pix_fmt_mask parameter.
 *
 * @code
 * src_pix_fmt = PIX_FMT_YUV420P;
 * pix_fmt_mask = (1 << PIX_FMT_YUV422P) || (1 << PIX_FMT_RGB24);
 * dst_pix_fmt = avcodec_find_best_pix_fmt(pix_fmt_mask, src_pix_fmt, alpha, &loss);
 * @endcode
 *
 * @param[in] pix_fmt_mask Bitmask determining which pixel format to choose from.
 * @param[in] src_pix_fmt Source pixel format.
 * @param[in] has_alpha Whether the source pixel format alpha channel is used.
 * @param[out] loss_ptr Combination of flags informing you what kind of losses will occur.
 * @return The best pixel format to convert to or -1 if none was found.
 */
int avcodec_find_best_pix_fmt(int pix_fmt_mask, int src_pix_fmt,
                              int has_alpha, int *loss_ptr);

#define FF_ALPHA_TRANSP       0x0001 /* image has some totally transparent pixels */
#define FF_ALPHA_SEMI_TRANSP  0x0002 /* image has some transparent pixels */

/**
 * Tell if an image really has transparent alpha values.
 * @return ored mask of FF_ALPHA_xxx constants
 */
int img_get_alpha_info(const AVPicture *src,
                       int pix_fmt, int width, int height);

#if LIBAVCODEC_VERSION_INT < ((52<<16)+(0<<8)+0)
/* convert among pixel formats */
attribute_deprecated int img_convert(AVPicture *dst, int dst_pix_fmt,
                const AVPicture *src, int pix_fmt,
                int width, int height);
#endif

/* deinterlace a picture */
/* deinterlace - if not supported return -1 */
int avpicture_deinterlace(AVPicture *dst, const AVPicture *src,
                          int pix_fmt, int width, int height);

/* external high level API */

extern AVCodec *first_avcodec;

/* returns LIBAVCODEC_VERSION_INT constant */
unsigned avcodec_version(void);
/* returns LIBAVCODEC_BUILD constant */
unsigned avcodec_build(void);

/**
 * Initializes libavcodec.
 *
 * @warning This function \e must be called before any other libavcodec
 * function.
 */
void avcodec_init(void);

void register_avcodec(AVCodec *format);

/**
 * Finds an encoder with a matching codec ID.
 *
 * @param id CodecID of the requested encoder.
 * @return An encoder if one was found, NULL otherwise.
 */
AVCodec *avcodec_find_encoder(enum CodecID id);

/**
 * Finds an encoder with the specified name.
 *
 * @param name Name of the requested encoder.
 * @return An encoder if one was found, NULL otherwise.
 */
AVCodec *avcodec_find_encoder_by_name(const char *name);

/**
 * Finds a decoder with a matching codec ID.
 *
 * @param id CodecID of the requested decoder.
 * @return A decoder if one was found, NULL otherwise.
 */
AVCodec *avcodec_find_decoder(enum CodecID id);

/**
 * Finds an decoder with the specified name.
 *
 * @param name Name of the requested decoder.
 * @return A decoder if one was found, NULL otherwise.
 */
AVCodec *avcodec_find_decoder_by_name(const char *name);
void avcodec_string(char *buf, int buf_size, AVCodecContext *enc, int encode);

/**
 * Sets the fields of the given AVCodecContext to default values.
 *
 * @param s The AVCodecContext of which the fields should be set to default values.
 */
void avcodec_get_context_defaults(AVCodecContext *s);

/** THIS FUNCTION IS NOT YET PART OF THE PUBLIC API!
 *  we WILL change its arguments and name a few times! */
void avcodec_get_context_defaults2(AVCodecContext *s, enum CodecType);

/**
 * Allocates an AVCodecContext and sets its fields to default values.  The
 * resulting struct can be deallocated by simply calling av_free().
 *
 * @return An AVCodecContext filled with default values or NULL on failure.
 * @see avcodec_get_context_defaults
 */
AVCodecContext *avcodec_alloc_context(void);

/** THIS FUNCTION IS NOT YET PART OF THE PUBLIC API!
 *  we WILL change its arguments and name a few times! */
AVCodecContext *avcodec_alloc_context2(enum CodecType);

/**
 * Sets the fields of the given AVFrame to default values.
 *
 * @param pic The AVFrame of which the fields should be set to default values.
 */
void avcodec_get_frame_defaults(AVFrame *pic);

/**
 * Allocates an AVFrame and sets its fields to default values.  The resulting
 * struct can be deallocated by simply calling av_free().
 *
 * @return An AVFrame filled with default values or NULL on failure.
 * @see avcodec_get_frame_defaults
 */
AVFrame *avcodec_alloc_frame(void);

int avcodec_default_get_buffer(AVCodecContext *s, AVFrame *pic);
void avcodec_default_release_buffer(AVCodecContext *s, AVFrame *pic);
int avcodec_default_reget_buffer(AVCodecContext *s, AVFrame *pic);
void avcodec_align_dimensions(AVCodecContext *s, int *width, int *height);

/**
 * Checks if the given dimension of a picture is valid, meaning that all
 * bytes of the picture can be addressed with a signed int.
 *
 * @param[in] w Width of the picture.
 * @param[in] h Height of the picture.
 * @return Zero if valid, a negative value if invalid.
 */
int avcodec_check_dimensions(void *av_log_ctx, unsigned int w, unsigned int h);
enum PixelFormat avcodec_default_get_format(struct AVCodecContext *s, const enum PixelFormat * fmt);

int avcodec_thread_init(AVCodecContext *s, int thread_count);
void avcodec_thread_free(AVCodecContext *s);
int avcodec_thread_execute(AVCodecContext *s, int (*func)(AVCodecContext *c2, void *arg2),void **arg, int *ret, int count);
int avcodec_default_execute(AVCodecContext *c, int (*func)(AVCodecContext *c2, void *arg2),void **arg, int *ret, int count);
//FIXME func typedef

/**
 * Initializes the AVCodecContext to use the given AVCodec. Prior to using this
 * function the context has to be allocated.
 *
 * The functions avcodec_find_decoder_by_name(), avcodec_find_encoder_by_name(),
 * avcodec_find_decoder() and avcodec_find_encoder() provide an easy way for
 * retrieving a codec.
 *
 * @warning This function is not thread save!
 *
 * @code
 * codec = avcodec_find_decoder(CODEC_ID_H264);
 * if (!codec)
 *     exit(1);
 *
 * context = avcodec_alloc_context();
 *
 * if (avcodec_open(context, codec) < 0)
 *     exit(1);
 * @endcode
 *
 * @param avctx The context which will be setup to use the given codec.
 * @param codec The codec to use within the context.
 * @return Zero on success, a negative value on error.
 * @see avcodec_alloc_context, avcodec_find_decoder, avcodec_find_encoder
 */
int avcodec_open(AVCodecContext *avctx, AVCodec *codec);

/**
 * @deprecated Use avcodec_decode_audio2() instead.
 */
attribute_deprecated int avcodec_decode_audio(AVCodecContext *avctx, int16_t *samples,
                         int *frame_size_ptr,
                         uint8_t *buf, int buf_size);

/**
 * Decodes an audio frame from \p buf into \p samples.
 * The avcodec_decode_audio2() function decodes a frame of audio from the input
 * buffer \p buf of size \p buf_size. To decode it, it makes use of the
 * audiocodec which was coupled with \p avctx using avcodec_open(). The
 * resulting decoded frame is stored in output buffer \p samples.  If no frame
 * could be decompressed, \p frame_size_ptr is zero. Otherwise, it is the
 * decompressed frame size in \e bytes.
 *
 * @warning You \e must set \p frame_size_ptr to the allocated size of the
 * output buffer before calling avcodec_decode_audio2().
 *
 * @warning The input buffer must be \c FF_INPUT_BUFFER_PADDING_SIZE larger than
 * the actual read bytes because some optimized bitstream readers read 32 or 64
 * bits at once and could read over the end.
 *
 * @warning The end of the input buffer \p buf should be set to 0 to ensure that
 * no overreading happens for damaged MPEG streams.
 *
 * @note You might have to align the input buffer \p buf and output buffer \p
 * samples. The alignment requirements depend on the CPU: on some CPUs it isn't
 * necessary at all, on others it won't work at all if not aligned and on others
 * it will work but it will have an impact on performance. In practice, the
 * bitstream should have 4 byte alignment at minimum and all sample data should
 * be 16 byte aligned unless the CPU doesn't need it (AltiVec and SSE do). If
 * the linesize is not a multiple of 16 then there's no sense in aligning the
 * start of the buffer to 16.
 *
 * @param avctx The codec context.
 * @param[out] samples The output buffer.
 * @param[in,out] frame_size_ptr The output buffer size in bytes.
 * @param[in] buf The input buffer.
 * @param[in] buf_size The input buffer size in bytes.
 * @return On error a negative value is returned, otherwise the number of bytes
 * used or zero if no frame could be decompressed.
 */
int avcodec_decode_audio2(AVCodecContext *avctx, int16_t *samples,
                         int *frame_size_ptr,
                         uint8_t *buf, int buf_size);

/**
 * Decodes a video frame from \p buf into \p picture.
 * The avcodec_decode_video() function decodes a frame of video from the input
 * buffer \p buf of size \p buf_size. To decode it, it makes use of the
 * videocodec which was coupled with \p avctx using avcodec_open(). The
 * resulting decoded frame is stored in \p picture.
 *
 * @warning The input buffer must be \c FF_INPUT_BUFFER_PADDING_SIZE larger than
 * the actual read bytes because some optimized bitstream readers read 32 or 64
 * bits at once and could read over the end.
 *
 * @warning The end of the input buffer \p buf should be set to 0 to ensure that
 * no overreading happens for damaged MPEG streams.
 *
 * @note You might have to align the input buffer \p buf and output buffer \p
 * samples. The alignment requirements depend on the CPU: on some CPUs it isn't
 * necessary at all, on others it won't work at all if not aligned and on others
 * it will work but it will have an impact on performance. In practice, the
 * bitstream should have 4 byte alignment at minimum and all sample data should
 * be 16 byte aligned unless the CPU doesn't need it (AltiVec and SSE do). If
 * the linesize is not a multiple of 16 then there's no sense in aligning the
 * start of the buffer to 16.
 *
 * @param avctx The codec context.
 * @param[out] picture The AVFrame in which the decoded video frame will be stored.
 * @param[in] buf The input buffer.
 * @param[in] buf_size The size of the input buffer in bytes.
 * @param[in,out] got_picture_ptr Zero if no frame could be decompressed, otherwise, it is non zero.
 * @return On error a negative value is returned, otherwise the number of bytes
 * used or zero if no frame could be decompressed.
 */
int avcodec_decode_video(AVCodecContext *avctx, AVFrame *picture,
                         int *got_picture_ptr,
                         uint8_t *buf, int buf_size);

/* decode a subtitle message. return -1 if error, otherwise return the
   *number of bytes used. If no subtitle could be decompressed,
   *got_sub_ptr is zero. Otherwise, the subtitle is stored in *sub. */
int avcodec_decode_subtitle(AVCodecContext *avctx, AVSubtitle *sub,
                            int *got_sub_ptr,
                            const uint8_t *buf, int buf_size);
int avcodec_parse_frame(AVCodecContext *avctx, uint8_t **pdata,
                        int *data_size_ptr,
                        uint8_t *buf, int buf_size);

/**
 * Encodes an audio frame from \p samples into \p buf.
 * The avcodec_encode_audio() function encodes a frame of audio from the input
 * buffer \p samples. To encode it, it makes use of the audiocodec which was
 * coupled with \p avctx using avcodec_open(). The resulting encoded frame is
 * stored in output buffer \p buf.
 *
 * @note The output buffer should be at least \c FF_MIN_BUFFER_SIZE bytes large.
 *
 * @param avctx The codec context.
 * @param[out] buf The output buffer.
 * @param[in] buf_size The output buffer size.
 * @param[in] samples The input buffer containing the samples.
 * @return On error a negative value is returned, on succes zero or the number
 * of bytes used from the input buffer.
 */
int avcodec_encode_audio(AVCodecContext *avctx, uint8_t *buf, int buf_size,
                         const short *samples);

/**
 * Encodes a video frame from \p pict into \p buf.
 * The avcodec_encode_video() function encodes a frame of video from the input
 * \p pict. To encode it, it makes use of the videocodec which was coupled with
 * \p avctx using avcodec_open(). The resulting encoded bytes representing the
 * frame are stored in the output buffer \p buf. The input picture should be
 * stored using a specific format, namely \c avctx.pix_fmt.
 *
 * @param avctx The codec context.
 * @param[out] buf The output buffer for the bitstream of encoded frame.
 * @param[in] buf_size The size of the outputbuffer in bytes.
 * @param[in] pict The input picture to encode.
 * @return On error a negative value is returned, on success zero or the number
 * of bytes used from the input buffer.
 */
int avcodec_encode_video(AVCodecContext *avctx, uint8_t *buf, int buf_size,
                         const AVFrame *pict);
int avcodec_encode_subtitle(AVCodecContext *avctx, uint8_t *buf, int buf_size,
                            const AVSubtitle *sub);

int avcodec_close(AVCodecContext *avctx);

void avcodec_register_all(void);

/**
 * Flush buffers, should be called when seeking or when switching to a different stream.
 */
void avcodec_flush_buffers(AVCodecContext *avctx);

void avcodec_default_free_buffers(AVCodecContext *s);

/* misc useful functions */

/**
 * Returns a single letter to describe the given picture type \p pict_type.
 *
 * @param[in] pict_type The picture type.
 * @return A single character representing the picture type.
 */
char av_get_pict_type_char(int pict_type);

/**
 * Returns codec bits per sample.
 *
 * @param[in] codec_id The codec.
 * @return Number of bits per sample or zero if unknown for the given codec.
 */
int av_get_bits_per_sample(enum CodecID codec_id);

/* frame parsing */
typedef struct AVCodecParserContext {
    void *priv_data;
    struct AVCodecParser *parser;
    int64_t frame_offset; /* offset of the current frame */
    int64_t cur_offset; /* current offset
                           (incremented by each av_parser_parse()) */
    int64_t last_frame_offset; /* offset of the last frame */
    /* video info */
    int pict_type; /* XXX: put it back in AVCodecContext */
    int repeat_pict; /* XXX: put it back in AVCodecContext */
    int64_t pts;     /* pts of the current frame */
    int64_t dts;     /* dts of the current frame */

    /* private data */
    int64_t last_pts;
    int64_t last_dts;
    int fetch_timestamp;

#define AV_PARSER_PTS_NB 4
    int cur_frame_start_index;
    int64_t cur_frame_offset[AV_PARSER_PTS_NB];
    int64_t cur_frame_pts[AV_PARSER_PTS_NB];
    int64_t cur_frame_dts[AV_PARSER_PTS_NB];

    int flags;
#define PARSER_FLAG_COMPLETE_FRAMES           0x0001
} AVCodecParserContext;

typedef struct AVCodecParser {
    int codec_ids[5]; /* several codec IDs are permitted */
    int priv_data_size;
    int (*parser_init)(AVCodecParserContext *s);
    int (*parser_parse)(AVCodecParserContext *s,
                        AVCodecContext *avctx,
                        uint8_t **poutbuf, int *poutbuf_size,
                        const uint8_t *buf, int buf_size);
    void (*parser_close)(AVCodecParserContext *s);
    int (*split)(AVCodecContext *avctx, const uint8_t *buf, int buf_size);
    struct AVCodecParser *next;
} AVCodecParser;

extern AVCodecParser *av_first_parser;

void av_register_codec_parser(AVCodecParser *parser);
AVCodecParserContext *av_parser_init(int codec_id);
int av_parser_parse(AVCodecParserContext *s,
                    AVCodecContext *avctx,
                    uint8_t **poutbuf, int *poutbuf_size,
                    const uint8_t *buf, int buf_size,
                    int64_t pts, int64_t dts);
int av_parser_change(AVCodecParserContext *s,
                     AVCodecContext *avctx,
                     uint8_t **poutbuf, int *poutbuf_size,
                     const uint8_t *buf, int buf_size, int keyframe);
void av_parser_close(AVCodecParserContext *s);

extern AVCodecParser aac_parser;
extern AVCodecParser ac3_parser;
extern AVCodecParser cavsvideo_parser;
extern AVCodecParser dca_parser;
extern AVCodecParser dvbsub_parser;
extern AVCodecParser dvdsub_parser;
extern AVCodecParser h261_parser;
extern AVCodecParser h263_parser;
extern AVCodecParser h264_parser;
extern AVCodecParser mjpeg_parser;
extern AVCodecParser mpeg4video_parser;
extern AVCodecParser mpegaudio_parser;
extern AVCodecParser mpegvideo_parser;
extern AVCodecParser pnm_parser;
extern AVCodecParser vc1_parser;


typedef struct AVBitStreamFilterContext {
    void *priv_data;
    struct AVBitStreamFilter *filter;
    AVCodecParserContext *parser;
    struct AVBitStreamFilterContext *next;
} AVBitStreamFilterContext;


typedef struct AVBitStreamFilter {
    const char *name;
    int priv_data_size;
    int (*filter)(AVBitStreamFilterContext *bsfc,
                  AVCodecContext *avctx, const char *args,
                  uint8_t **poutbuf, int *poutbuf_size,
                  const uint8_t *buf, int buf_size, int keyframe);
    struct AVBitStreamFilter *next;
} AVBitStreamFilter;

extern AVBitStreamFilter *av_first_bitstream_filter;

void av_register_bitstream_filter(AVBitStreamFilter *bsf);
AVBitStreamFilterContext *av_bitstream_filter_init(const char *name);
int av_bitstream_filter_filter(AVBitStreamFilterContext *bsfc,
                               AVCodecContext *avctx, const char *args,
                               uint8_t **poutbuf, int *poutbuf_size,
                               const uint8_t *buf, int buf_size, int keyframe);
void av_bitstream_filter_close(AVBitStreamFilterContext *bsf);

extern AVBitStreamFilter dump_extradata_bsf;
extern AVBitStreamFilter remove_extradata_bsf;
extern AVBitStreamFilter noise_bsf;
extern AVBitStreamFilter mp3_header_compress_bsf;
extern AVBitStreamFilter mp3_header_decompress_bsf;
extern AVBitStreamFilter mjpega_dump_header_bsf;
extern AVBitStreamFilter imx_dump_header_bsf;


/* memory */

/**
 * Reallocates the given block if it is not large enough, otherwise it
 * does nothing.
 *
 * @see av_realloc
 */
void *av_fast_realloc(void *ptr, unsigned int *size, unsigned int min_size);

/* for static data only */

/**
 * Frees all static arrays and reset their pointers to 0.
 * Call this function to release all statically allocated tables.
 */
attribute_deprecated void av_free_static(void);

/**
 * Allocation of static arrays.
 *
 * @warning Do not use for normal allocation.
 *
 * @param[in] size The amount of memory you need in bytes.
 * @return Block of memory of the requested size.
 */
attribute_deprecated void *av_mallocz_static(unsigned int size);

/**
 * Copy image 'src' to 'dst'.
 */
void av_picture_copy(AVPicture *dst, const AVPicture *src,
              int pix_fmt, int width, int height);

/**
 * Crop image top and left side
 */
int av_picture_crop(AVPicture *dst, const AVPicture *src,
             int pix_fmt, int top_band, int left_band);

/**
 * Pad image
 */
int av_picture_pad(AVPicture *dst, const AVPicture *src, int height, int width, int pix_fmt,
            int padtop, int padbottom, int padleft, int padright, int *color);

#if LIBAVCODEC_VERSION_INT < ((52<<16)+(0<<8)+0)
attribute_deprecated void img_copy(AVPicture *dst, const AVPicture *src,
              int pix_fmt, int width, int height);

attribute_deprecated int img_crop(AVPicture *dst, const AVPicture *src,
             int pix_fmt, int top_band, int left_band);

attribute_deprecated int img_pad(AVPicture *dst, const AVPicture *src, int height, int width, int pix_fmt,
            int padtop, int padbottom, int padleft, int padright, int *color);
#endif

extern unsigned int av_xiphlacing(unsigned char *s, unsigned int v);

/* error handling */
#if EINVAL > 0
#define AVERROR(e) (-(e)) /**< returns a negative error code from a POSIX error code, to return from library functions. */
#define AVUNERROR(e) (-(e)) /**< returns a POSIX error code from a library function error return value. */
#else
/* some platforms have E* and errno already negated. */
#define AVERROR(e) (e)
#define AVUNERROR(e) (e)
#endif
#define AVERROR_UNKNOWN     AVERROR(EINVAL)  /**< unknown error */
#define AVERROR_IO          AVERROR(EIO)     /**< i/o error */
#define AVERROR_NUMEXPECTED AVERROR(EDOM)    /**< number syntax expected in filename */
#define AVERROR_INVALIDDATA AVERROR(EINVAL)  /**< invalid data found */
#define AVERROR_NOMEM       AVERROR(ENOMEM)  /**< not enough memory */
#define AVERROR_NOFMT       AVERROR(EILSEQ)  /**< unknown format */
#define AVERROR_NOTSUPP     AVERROR(ENOSYS)  /**< operation not supported */

#ifdef __cplusplus
}
#endif

#endif /* AVCODEC_H */
