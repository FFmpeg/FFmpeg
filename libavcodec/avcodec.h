#ifndef AVCODEC_H
#define AVCODEC_H

/**
 * @file avcodec.h
 * external api header.
 */


#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include "rational.h"
#include <sys/types.h> /* size_t */

#define FFMPEG_VERSION_INT     0x000409
#define FFMPEG_VERSION         "0.4.9-pre1"
#define LIBAVCODEC_BUILD       4718

#define LIBAVCODEC_VERSION_INT FFMPEG_VERSION_INT
#define LIBAVCODEC_VERSION     FFMPEG_VERSION

#define AV_STRINGIFY(s)	AV_TOSTRING(s)
#define AV_TOSTRING(s) #s
#define LIBAVCODEC_IDENT	"FFmpeg" LIBAVCODEC_VERSION "b" AV_STRINGIFY(LIBAVCODEC_BUILD)

#define AV_NOPTS_VALUE int64_t_C(0x8000000000000000)
#define AV_TIME_BASE 1000000

enum CodecID {
    CODEC_ID_NONE, 
    CODEC_ID_MPEG1VIDEO,
    CODEC_ID_MPEG2VIDEO, /* prefered ID for MPEG Video 1 or 2 decoding */
    CODEC_ID_MPEG2VIDEO_XVMC,
    CODEC_ID_H261,
    CODEC_ID_H263,
    CODEC_ID_RV10,
    CODEC_ID_RV20,
    CODEC_ID_MP2,
    CODEC_ID_MP3, /* prefered ID for MPEG Audio layer 1, 2 or3 decoding */
    CODEC_ID_VORBIS,
    CODEC_ID_AC3,
    CODEC_ID_MJPEG,
    CODEC_ID_MJPEGB,
    CODEC_ID_LJPEG,
    CODEC_ID_SP5X,
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
    CODEC_ID_DVAUDIO,
    CODEC_ID_WMAV1,
    CODEC_ID_WMAV2,
    CODEC_ID_MACE3,
    CODEC_ID_MACE6,
    CODEC_ID_HUFFYUV,
    CODEC_ID_CYUV,
    CODEC_ID_H264,
    CODEC_ID_INDEO3,
    CODEC_ID_VP3,
    CODEC_ID_THEORA,
    CODEC_ID_AAC,
    CODEC_ID_MPEG4AAC,
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
    CODEC_ID_VMDAUDIO,
    CODEC_ID_MSZH,
    CODEC_ID_ZLIB,
    CODEC_ID_QTRLE,

    /* various pcm "codecs" */
    CODEC_ID_PCM_S16LE,
    CODEC_ID_PCM_S16BE,
    CODEC_ID_PCM_U16LE,
    CODEC_ID_PCM_U16BE,
    CODEC_ID_PCM_S8,
    CODEC_ID_PCM_U8,
    CODEC_ID_PCM_MULAW,
    CODEC_ID_PCM_ALAW,

    /* various adpcm codecs */
    CODEC_ID_ADPCM_IMA_QT,
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

	/* AMR */
    CODEC_ID_AMR_NB,
    CODEC_ID_AMR_WB,

    /* RealAudio codecs*/
    CODEC_ID_RA_144,
    CODEC_ID_RA_288,

    /* various DPCM codecs */
    CODEC_ID_ROQ_DPCM,
    CODEC_ID_INTERPLAY_DPCM,
    CODEC_ID_XAN_DPCM,
    
    CODEC_ID_FLAC,
    
    CODEC_ID_MPEG2TS, /* _FAKE_ codec to indicate a raw MPEG2 transport
                         stream (only used by libavformat) */
};

/* CODEC_ID_MP3LAME is absolete */
#define CODEC_ID_MP3LAME CODEC_ID_MP3

enum CodecType {
    CODEC_TYPE_UNKNOWN = -1,
    CODEC_TYPE_VIDEO,
    CODEC_TYPE_AUDIO,
    CODEC_TYPE_DATA,
};

/**
 * Pixel format. Notes: 
 *
 * PIX_FMT_RGBA32 is handled in an endian-specific manner. A RGBA
 * color is put together as:
 *  (A << 24) | (R << 16) | (G << 8) | B
 * This is stored as BGRA on little endian CPU architectures and ARGB on
 * big endian CPUs.
 *
 * When the pixel format is palettized RGB (PIX_FMT_PAL8), the palettized
 * image data is stored in AVFrame.data[0]. The palette is transported in
 * AVFrame.data[1] and, is 1024 bytes long (256 4-byte entries) and is
 * formatted the same as in PIX_FMT_RGBA32 described above (i.e., it is
 * also endian-specific). Note also that the individual RGB palette
 * components stored in AVFrame.data[1] should be in the range 0..255.
 * This is important as many custom PAL8 video codecs that were designed
 * to run on the IBM VGA graphics adapter use 6-bit palette components.
 */
enum PixelFormat {
    PIX_FMT_YUV420P,   ///< Planar YUV 4:2:0 (1 Cr & Cb sample per 2x2 Y samples)
    PIX_FMT_YUV422,    
    PIX_FMT_RGB24,     ///< Packed pixel, 3 bytes per pixel, RGBRGB...
    PIX_FMT_BGR24,     ///< Packed pixel, 3 bytes per pixel, BGRBGR...
    PIX_FMT_YUV422P,   ///< Planar YUV 4:2:2 (1 Cr & Cb sample per 2x1 Y samples)
    PIX_FMT_YUV444P,   ///< Planar YUV 4:4:4 (1 Cr & Cb sample per 1x1 Y samples)
    PIX_FMT_RGBA32,    ///< Packed pixel, 4 bytes per pixel, BGRABGRA..., stored in cpu endianness
    PIX_FMT_YUV410P,   ///< Planar YUV 4:1:0 (1 Cr & Cb sample per 4x4 Y samples)
    PIX_FMT_YUV411P,   ///< Planar YUV 4:1:1 (1 Cr & Cb sample per 4x1 Y samples)
    PIX_FMT_RGB565,    ///< always stored in cpu endianness 
    PIX_FMT_RGB555,    ///< always stored in cpu endianness, most significant bit to 1 
    PIX_FMT_GRAY8,
    PIX_FMT_MONOWHITE, ///< 0 is white 
    PIX_FMT_MONOBLACK, ///< 0 is black 
    PIX_FMT_PAL8,      ///< 8 bit with RGBA palette 
    PIX_FMT_YUVJ420P,  ///< Planar YUV 4:2:0 full scale (jpeg)
    PIX_FMT_YUVJ422P,  ///< Planar YUV 4:2:2 full scale (jpeg)
    PIX_FMT_YUVJ444P,  ///< Planar YUV 4:4:4 full scale (jpeg)
    PIX_FMT_XVMC_MPEG2_MC,///< XVideo Motion Acceleration via common packet passing(xvmc_render.h)
    PIX_FMT_XVMC_MPEG2_IDCT,
    PIX_FMT_NB,
};

/* currently unused, may be used if 24/32 bits samples ever supported */
enum SampleFormat {
    SAMPLE_FMT_S16 = 0,         ///< signed 16 bits 
};

/* in bytes */
#define AVCODEC_MAX_AUDIO_FRAME_SIZE 131072

/**
 * Required number of additionally allocated bytes at the end of the input bitstream for decoding.
 * this is mainly needed because some optimized bitstream readers read 
 * 32 or 64 bit at once and could read over the end<br>
 * Note, if the first 23 bits of the additional bytes are not 0 then damaged
 * MPEG bitstreams could cause overread and segfault
 */
#define FF_INPUT_BUFFER_PADDING_SIZE 8

/* motion estimation type, EPZS by default */
enum Motion_Est_ID {
    ME_ZERO = 1,
    ME_FULL,
    ME_LOG,
    ME_PHODS,
    ME_EPZS,
    ME_X1
};

typedef struct RcOverride{
    int start_frame;
    int end_frame;
    int qscale; // if this is 0 then quality_factor will be used instead
    float quality_factor;
} RcOverride;

/* only for ME compatiblity with old apps */
extern int motion_estimation_method;

#define FF_MAX_B_FRAMES 8

/* encoding support
   these flags can be passed in AVCodecContext.flags before initing 
   Note: note not everything is supported yet 
*/

#define CODEC_FLAG_QSCALE 0x0002  ///< use fixed qscale 
#define CODEC_FLAG_4MV    0x0004  ///< 4 MV per MB allowed / Advanced prediction for H263
#define CODEC_FLAG_QPEL   0x0010  ///< use qpel MC 
#define CODEC_FLAG_GMC    0x0020  ///< use GMC 
#define CODEC_FLAG_MV0    0x0040  ///< always try a MB with MV=<0,0> 
#define CODEC_FLAG_PART   0x0080  ///< use data partitioning 
/* parent program gurantees that the input for b-frame containing streams is not written to 
   for at least s->max_b_frames+1 frames, if this is not set than the input will be copied */
#define CODEC_FLAG_INPUT_PRESERVED 0x0100
#define CODEC_FLAG_PASS1 0x0200   ///< use internal 2pass ratecontrol in first  pass mode 
#define CODEC_FLAG_PASS2 0x0400   ///< use internal 2pass ratecontrol in second pass mode 
#define CODEC_FLAG_EXTERN_HUFF 0x1000 ///< use external huffman table (for mjpeg) 
#define CODEC_FLAG_GRAY  0x2000   ///< only decode/encode grayscale 
#define CODEC_FLAG_EMU_EDGE 0x4000///< dont draw edges 
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
#define CODEC_FLAG_H263P_AIC      0x01000000 ///< H263 Advanced intra coding / MPEG4 AC prediction (remove this)
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
#define CODEC_FLAG_CLOSED_GOP     0x80000000
/* Unsupported options :
 * 		Syntax Arithmetic coding (SAC)
 * 		Reference Picture Selection
 * 		Independant Segment Decoding */
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
/** codec has a non zero delay and needs to be feeded with NULL at the end to get the delayed data */
#define CODEC_CAP_DELAY           0x0020

//the following defines might change, so dont expect compatibility if u use them
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
     * this isnt used by lavc unless the default get/release_buffer() is used\
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
     * presentation timestamp in AV_TIME_BASE (=micro seconds currently) (time when frame should be shown to user)\
     * if AV_NOPTS_VALUE then the frame_rate will be used as reference\
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
     * Motion vector table\
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
     * Macroblock size: (0->16x16, 1->8x8, 2-> 4x4, 3-> 2x2)\
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

#define FF_QSCALE_TYPE_MPEG1	0
#define FF_QSCALE_TYPE_MPEG2	1

#define FF_BUFFER_TYPE_INTERNAL 1
#define FF_BUFFER_TYPE_USER     2 ///< Direct rendering buffers (image is (de)allocated by user)
#define FF_BUFFER_TYPE_SHARED   4 ///< buffer from somewher else, dont dealloc image (data/base), all other tables are not shared
#define FF_BUFFER_TYPE_COPY     8 ///< just a (modified) copy of some other buffer, dont dealloc anything


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
 * Used by av_log
 */
typedef struct AVCLASS AVClass;
struct AVCLASS {
    const char* class_name;
    const char* (*item_name)(void*); /* actually passing a pointer to an AVCodecContext
					or AVFormatContext, which begin with an AVClass.
					Needed because av_log is in libavcodec and has no visibility
					of AVIn/OutputFormat */
};

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
     * - encoding: set by user. 
     * - decoding: set by lavc. (FIXME is this ok?)
     */
    int sub_id;

    /**
     * motion estimation algorithm used for video coding.
     * - encoding: MUST be set by user.
     * - decoding: unused
     */
    int me_method;

    /**
     * some codecs need / can use extra-data like huffman tables.
     * mjpeg: huffman tables
     * rv10: additional flags
     * mpeg4: global headers (they can be in the bitstream or here)
     * - encoding: set/allocated/freed by lavc.
     * - decoding: set/allocated/freed by user.
     */
    void *extradata;
    int extradata_size;
    
    /* video only */
    /**
     * frames per sec multiplied by frame_rate_base.
     * for variable fps this is the precission, so if the timestamps 
     * can be specified in msec precssion then this is 1000*frame_rate_base
     * - encoding: MUST be set by user
     * - decoding: set by lavc. 0 or the frame_rate if available
     */
    int frame_rate;
    
    /**
     * width / height.
     * - encoding: MUST be set by user. 
     * - decoding: set by user if known, codec should override / dynamically change if needed
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
     * - encoding: FIXME: used by ffmpeg to decide whether an pix_fmt
     *                    conversion is in order. This only works for
     *                    codecs with one supported pix_fmt, we should
     *                    do something for a generic case as well.
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
    int sample_fmt;  ///< sample format, currenly unused 

    /* the following data should not be initialized */
    int frame_size;     ///< in samples, initialized when calling 'init' 
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
     * maximum quantizer difference etween frames.
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
    int b_frame_strategy;

    /**
     * hurry up amount.
     * - encoding: unused
     * - decoding: set by user. 1-> skip b frames, 2-> skip idct/dequant too, 5-> skip everything except header
     */
    int hurry_up;
    
    struct AVCodec *codec;
    
    void *priv_data;

    /* unused, FIXME remove*/
    int rtp_mode;
    
    int rtp_payload_size;   /* The size of the RTP payload, the coder will  */
                            /* do it's best to deliver a chunk with size    */
                            /* below rtp_payload_size, the chunk will start */
                            /* with a start code on some codecs like H.263  */
                            /* This doesn't take account of any particular  */
                            /* headers inside the transmited RTP payload    */

    
    /* The RTP callcack: This function is called  */
    /* every time the encoder as a packet to send */
    /* Depends on the encoder if the data starts  */
    /* with a Start Code (it should) H.263 does   */
    void (*rtp_callback)(struct AVCodecContext *avctx, void *data, int size, int packet_number); 

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
     * - encoding: set by user, if not then the default based on codec_id will be used
     * - decoding: set by user, will be converted to upper case by lavc during init
     */
    unsigned int codec_tag;
    
    /**
     * workaround bugs in encoders which sometimes cannot be detected automatically.
     * - encoding: unused
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
#define FF_ER_CAREFULL        1
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
    
    int block_align; ///< used by some WAV based audio codecs
    
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
#define FF_MM_FORCE	0x80000000 /* force usage of selected flags (OR) */
    /* lower 16 bits - CPU features */
#ifdef HAVE_MMX
#define FF_MM_MMX	0x0001 /* standard MMX */
#define FF_MM_3DNOW	0x0004 /* AMD 3DNOW */
#define FF_MM_MMXEXT	0x0002 /* SSE integer functions or AMD MMX ext */
#define FF_MM_SSE	0x0008 /* SSE functions */
#define FF_MM_SSE2	0x0010 /* PIV SSE2 functions */
#endif /* HAVE_MMX */

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
     * - encoding: set by user.
     * - decoding: unused
     */
    int mb_qmin;

    /**
     * maximum MB quantizer.
     * - encoding: set by user.
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
     * frame_rate_base.
     * for variable fps this is 1
     * - encoding: set by user.
     * - decoding: set by lavc.
     * @todo move this after frame_rate
     */

    int frame_rate_base;
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
     * Dont touch, used by lavc default_get_buffer()
     */
    int internal_buffer_count;
    
    /**
     * internal_buffers. 
     * Dont touch, used by lavc default_get_buffer()
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
    
#define FF_CODER_TYPE_VLC   0
#define FF_CODER_TYPE_AC    1
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
     * is used to decide how many independant tasks should be passed to execute()
     * - encoding: set by user
     * - decoding: set by user
     */
    int thread_count;
    
    /**
     * the codec may call this to execute several independant things. it will return only after
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
     * number of macroblock rows at the top which are skiped.
     * - encoding: unused
     * - decoding: set by user
     */
     int skip_top;

    /**
     * number of macroblock rows at the bottom which are skiped.
     * - encoding: unused
     * - decoding: set by user
     */
     int skip_bottom;
} AVCodecContext;


/**
 * AVOption.
 */
typedef struct AVOption {
    /** options' name */
    const char *name; /* if name is NULL, it indicates a link to next */
    /** short English text help or const struct AVOption* subpointer */
    const char *help; //	const struct AVOption* sub;
    /** offset to context structure where the parsed value should be stored */
    int offset;
    /** options' type */
    int type;
#define FF_OPT_TYPE_BOOL 1      ///< boolean - true,1,on  (or simply presence)
#define FF_OPT_TYPE_DOUBLE 2    ///< double
#define FF_OPT_TYPE_INT 3       ///< integer
#define FF_OPT_TYPE_STRING 4    ///< string (finished with \0)
#define FF_OPT_TYPE_MASK 0x1f	///< mask for types - upper bits are various flags
//#define FF_OPT_TYPE_EXPERT 0x20 // flag for expert option
#define FF_OPT_TYPE_FLAG (FF_OPT_TYPE_BOOL | 0x40)
#define FF_OPT_TYPE_RCOVERRIDE (FF_OPT_TYPE_STRING | 0x80)
    /** min value  (min == max   ->  no limits) */
    double min;
    /** maximum value for double/int */
    double max;
    /** default boo [0,1]l/double/int value */
    double defval;
    /**
     * default string value (with optional semicolon delimited extra option-list
     * i.e.   option1;option2;option3
     * defval might select other then first argument as default
     */
    const char *defstr;
#define FF_OPT_MAX_DEPTH 10
} AVOption;

/**
 * Parse option(s) and sets fields in passed structure
 * @param strct	structure where the parsed results will be written
 * @param list  list with AVOptions
 * @param opts	string with options for parsing
 */
int avoption_parse(void* strct, const AVOption* list, const char* opts);


/**
 * AVCodec.
 */
typedef struct AVCodec {
    const char *name;
    enum CodecType type;
    int id;
    int priv_data_size;
    int (*init)(AVCodecContext *);
    int (*encode)(AVCodecContext *, uint8_t *buf, int buf_size, void *data);
    int (*close)(AVCodecContext *);
    int (*decode)(AVCodecContext *, void *outdata, int *outdata_size,
                  uint8_t *buf, int buf_size);
    int capabilities;
    const AVOption *options;
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

} AVPaletteControl;

extern AVCodec ac3_encoder;
extern AVCodec mp2_encoder;
extern AVCodec mp3lame_encoder;
extern AVCodec oggvorbis_encoder;
extern AVCodec faac_encoder;
extern AVCodec mpeg1video_encoder;
extern AVCodec mpeg2video_encoder;
extern AVCodec h263_encoder;
extern AVCodec h263p_encoder;
extern AVCodec flv_encoder;
extern AVCodec rv10_encoder;
extern AVCodec rv20_encoder;
extern AVCodec dvvideo_encoder;
extern AVCodec mjpeg_encoder;
extern AVCodec ljpeg_encoder;
extern AVCodec mpeg4_encoder;
extern AVCodec msmpeg4v1_encoder;
extern AVCodec msmpeg4v2_encoder;
extern AVCodec msmpeg4v3_encoder;
extern AVCodec wmv1_encoder;
extern AVCodec wmv2_encoder;
extern AVCodec huffyuv_encoder;
extern AVCodec h264_encoder;
extern AVCodec asv1_encoder;
extern AVCodec asv2_encoder;
extern AVCodec vcr1_encoder;
extern AVCodec ffv1_encoder;
extern AVCodec mdec_encoder;
extern AVCodec zlib_encoder;
extern AVCodec svq1_encoder;

extern AVCodec h263_decoder;
extern AVCodec h261_decoder;
extern AVCodec mpeg4_decoder;
extern AVCodec msmpeg4v1_decoder;
extern AVCodec msmpeg4v2_decoder;
extern AVCodec msmpeg4v3_decoder;
extern AVCodec wmv1_decoder;
extern AVCodec wmv2_decoder;
extern AVCodec mpeg1video_decoder;
extern AVCodec mpeg2video_decoder;
extern AVCodec mpegvideo_decoder;
extern AVCodec mpeg_xvmc_decoder;
extern AVCodec h263i_decoder;
extern AVCodec flv_decoder;
extern AVCodec rv10_decoder;
extern AVCodec rv20_decoder;
extern AVCodec svq1_decoder;
extern AVCodec svq3_decoder;
extern AVCodec dvvideo_decoder;
extern AVCodec wmav1_decoder;
extern AVCodec wmav2_decoder;
extern AVCodec mjpeg_decoder;
extern AVCodec mjpegb_decoder;
extern AVCodec sp5x_decoder;
extern AVCodec mp2_decoder;
extern AVCodec mp3_decoder;
extern AVCodec mace3_decoder;
extern AVCodec mace6_decoder;
extern AVCodec huffyuv_decoder;
extern AVCodec oggvorbis_decoder;
extern AVCodec cyuv_decoder;
extern AVCodec h264_decoder;
extern AVCodec indeo3_decoder;
extern AVCodec vp3_decoder;
extern AVCodec theora_decoder;
extern AVCodec amr_nb_decoder;
extern AVCodec amr_nb_encoder;
extern AVCodec amr_wb_encoder;
extern AVCodec amr_wb_decoder;
extern AVCodec aac_decoder;
extern AVCodec mpeg4aac_decoder;
extern AVCodec asv1_decoder;
extern AVCodec asv2_decoder;
extern AVCodec vcr1_decoder;
extern AVCodec cljr_decoder;
extern AVCodec ffv1_decoder;
extern AVCodec fourxm_decoder;
extern AVCodec mdec_decoder;
extern AVCodec roq_decoder;
extern AVCodec interplay_video_decoder;
extern AVCodec xan_wc3_decoder;
extern AVCodec rpza_decoder;
extern AVCodec cinepak_decoder;
extern AVCodec msrle_decoder;
extern AVCodec msvideo1_decoder;
extern AVCodec vqa_decoder;
extern AVCodec idcin_decoder;
extern AVCodec eightbps_decoder;
extern AVCodec smc_decoder;
extern AVCodec flic_decoder;
extern AVCodec vmdvideo_decoder;
extern AVCodec vmdaudio_decoder;
extern AVCodec truemotion1_decoder;
extern AVCodec mszh_decoder;
extern AVCodec zlib_decoder;
extern AVCodec ra_144_decoder;
extern AVCodec ra_288_decoder;
extern AVCodec roq_dpcm_decoder;
extern AVCodec interplay_dpcm_decoder;
extern AVCodec xan_dpcm_decoder;
extern AVCodec qtrle_decoder;
extern AVCodec flac_decoder;

/* pcm codecs */
#define PCM_CODEC(id, name) \
extern AVCodec name ## _decoder; \
extern AVCodec name ## _encoder

PCM_CODEC(CODEC_ID_PCM_S16LE, pcm_s16le);
PCM_CODEC(CODEC_ID_PCM_S16BE, pcm_s16be);
PCM_CODEC(CODEC_ID_PCM_U16LE, pcm_u16le);
PCM_CODEC(CODEC_ID_PCM_U16BE, pcm_u16be);
PCM_CODEC(CODEC_ID_PCM_S8, pcm_s8);
PCM_CODEC(CODEC_ID_PCM_U8, pcm_u8);
PCM_CODEC(CODEC_ID_PCM_ALAW, pcm_alaw);
PCM_CODEC(CODEC_ID_PCM_MULAW, pcm_mulaw);

/* adpcm codecs */

PCM_CODEC(CODEC_ID_ADPCM_IMA_QT, adpcm_ima_qt);
PCM_CODEC(CODEC_ID_ADPCM_IMA_WAV, adpcm_ima_wav);
PCM_CODEC(CODEC_ID_ADPCM_IMA_DK3, adpcm_ima_dk3);
PCM_CODEC(CODEC_ID_ADPCM_IMA_DK4, adpcm_ima_dk4);
PCM_CODEC(CODEC_ID_ADPCM_IMA_WS, adpcm_ima_ws);
PCM_CODEC(CODEC_ID_ADPCM_SMJPEG, adpcm_ima_smjpeg);
PCM_CODEC(CODEC_ID_ADPCM_MS, adpcm_ms);
PCM_CODEC(CODEC_ID_ADPCM_4XM, adpcm_4xm);
PCM_CODEC(CODEC_ID_ADPCM_XA, adpcm_xa);
PCM_CODEC(CODEC_ID_ADPCM_ADX, adpcm_adx);
PCM_CODEC(CODEC_ID_ADPCM_EA, adpcm_ea);
PCM_CODEC(CODEC_ID_ADPCM_G726, adpcm_g726);

#undef PCM_CODEC

/* dummy raw video codec */
extern AVCodec rawvideo_encoder;
extern AVCodec rawvideo_decoder;

/* the following codecs use external GPL libs */
extern AVCodec ac3_decoder;

/* resample.c */

struct ReSampleContext;
struct AVResampleContext;

typedef struct ReSampleContext ReSampleContext;

ReSampleContext *audio_resample_init(int output_channels, int input_channels, 
                                     int output_rate, int input_rate);
int audio_resample(ReSampleContext *s, short *output, short *input, int nb_samples);
void audio_resample_close(ReSampleContext *s);

struct AVResampleContext *av_resample_init(int out_rate, int in_rate);
int av_resample(struct AVResampleContext *c, short *dst, short *src, int *consumed, int src_size, int dst_size, int update_ctx);
void av_resample_compensate(struct AVResampleContext *c, int sample_delta, int compensation_distance);
void av_resample_close(struct AVResampleContext *c);

/* YUV420 format is assumed ! */

struct ImgReSampleContext;

typedef struct ImgReSampleContext ImgReSampleContext;

ImgReSampleContext *img_resample_init(int output_width, int output_height,
                                      int input_width, int input_height);

ImgReSampleContext *img_resample_full_init(int owidth, int oheight,
                                      int iwidth, int iheight,
                                      int topBand, int bottomBand,
                                      int leftBand, int rightBand,
                                      int padtop, int padbottom,
                                      int padleft, int padright);


void img_resample(ImgReSampleContext *s, 
                  AVPicture *output, const AVPicture *input);

void img_resample_close(ImgReSampleContext *s);

/**
 * Allocate memory for a picture.  Call avpicture_free to free it.
 *
 * @param picture the picture to be filled in.
 * @param pix_fmt the format of the picture.
 * @param width the width of the picture.
 * @param height the height of the picture.
 * @return 0 if successful, -1 if not.
 */
int avpicture_alloc(AVPicture *picture, int pix_fmt, int width, int height);

/* Free a picture previously allocated by avpicture_alloc. */
void avpicture_free(AVPicture *picture);

int avpicture_fill(AVPicture *picture, uint8_t *ptr,
                   int pix_fmt, int width, int height);
int avpicture_layout(const AVPicture* src, int pix_fmt, int width, int height,
                     unsigned char *dest, int dest_size);
int avpicture_get_size(int pix_fmt, int width, int height);
void avcodec_get_chroma_sub_sample(int pix_fmt, int *h_shift, int *v_shift);
const char *avcodec_get_pix_fmt_name(int pix_fmt);
enum PixelFormat avcodec_get_pix_fmt(const char* name);

#define FF_LOSS_RESOLUTION  0x0001 /* loss due to resolution change */
#define FF_LOSS_DEPTH       0x0002 /* loss due to color depth change */
#define FF_LOSS_COLORSPACE  0x0004 /* loss due to color space conversion */
#define FF_LOSS_ALPHA       0x0008 /* loss of alpha bits */
#define FF_LOSS_COLORQUANT  0x0010 /* loss due to color quantization */
#define FF_LOSS_CHROMA      0x0020 /* loss of chroma (e.g. rgb to gray conversion) */

int avcodec_get_pix_fmt_loss(int dst_pix_fmt, int src_pix_fmt,
                             int has_alpha);
int avcodec_find_best_pix_fmt(int pix_fmt_mask, int src_pix_fmt,
                              int has_alpha, int *loss_ptr);

#define FF_ALPHA_TRANSP       0x0001 /* image has some totally transparent pixels */
#define FF_ALPHA_SEMI_TRANSP  0x0002 /* image has some transparent pixels */
int img_get_alpha_info(const AVPicture *src,
		       int pix_fmt, int width, int height);

/* convert among pixel formats */
int img_convert(AVPicture *dst, int dst_pix_fmt,
                const AVPicture *src, int pix_fmt, 
                int width, int height);

/* deinterlace a picture */
int avpicture_deinterlace(AVPicture *dst, const AVPicture *src,
                          int pix_fmt, int width, int height);

/* external high level API */

extern AVCodec *first_avcodec;

/* returns LIBAVCODEC_VERSION_INT constant */
unsigned avcodec_version(void);
/* returns LIBAVCODEC_BUILD constant */
unsigned avcodec_build(void);
void avcodec_init(void);

void register_avcodec(AVCodec *format);
AVCodec *avcodec_find_encoder(enum CodecID id);
AVCodec *avcodec_find_encoder_by_name(const char *name);
AVCodec *avcodec_find_decoder(enum CodecID id);
AVCodec *avcodec_find_decoder_by_name(const char *name);
void avcodec_string(char *buf, int buf_size, AVCodecContext *enc, int encode);

void avcodec_get_context_defaults(AVCodecContext *s);
AVCodecContext *avcodec_alloc_context(void);
void avcodec_get_frame_defaults(AVFrame *pic);
AVFrame *avcodec_alloc_frame(void);

int avcodec_default_get_buffer(AVCodecContext *s, AVFrame *pic);
void avcodec_default_release_buffer(AVCodecContext *s, AVFrame *pic);
int avcodec_default_reget_buffer(AVCodecContext *s, AVFrame *pic);
void avcodec_align_dimensions(AVCodecContext *s, int *width, int *height);
enum PixelFormat avcodec_default_get_format(struct AVCodecContext *s, const enum PixelFormat * fmt);

int avcodec_thread_init(AVCodecContext *s, int thread_count);
void avcodec_thread_free(AVCodecContext *s);
int avcodec_thread_execute(AVCodecContext *s, int (*func)(AVCodecContext *c2, void *arg2),void **arg, int *ret, int count);
int avcodec_default_execute(AVCodecContext *c, int (*func)(AVCodecContext *c2, void *arg2),void **arg, int *ret, int count);
//FIXME func typedef

/**
 * opens / inits the AVCodecContext.
 * not thread save!
 */
int avcodec_open(AVCodecContext *avctx, AVCodec *codec);

int avcodec_decode_audio(AVCodecContext *avctx, int16_t *samples, 
                         int *frame_size_ptr,
                         uint8_t *buf, int buf_size);
int avcodec_decode_video(AVCodecContext *avctx, AVFrame *picture, 
                         int *got_picture_ptr,
                         uint8_t *buf, int buf_size);
int avcodec_parse_frame(AVCodecContext *avctx, uint8_t **pdata, 
                        int *data_size_ptr,
                        uint8_t *buf, int buf_size);
int avcodec_encode_audio(AVCodecContext *avctx, uint8_t *buf, int buf_size, 
                         const short *samples);
int avcodec_encode_video(AVCodecContext *avctx, uint8_t *buf, int buf_size, 
                         const AVFrame *pict);

int avcodec_close(AVCodecContext *avctx);

void avcodec_register_all(void);

void avcodec_flush_buffers(AVCodecContext *avctx);

/* misc usefull functions */

/**
 * returns a single letter to describe the picture type
 */
char av_get_pict_type_char(int pict_type);

/**
 * reduce a fraction.
 * this is usefull for framerate calculations
 * @param max the maximum allowed for dst_nom & dst_den
 * @return 1 if exact, 0 otherwise
 */
int av_reduce(int *dst_nom, int *dst_den, int64_t nom, int64_t den, int64_t max);

/**
 * rescale a 64bit integer.
 * a simple a*b/c isnt possible as it can overflow
 */
int64_t av_rescale(int64_t a, int64_t b, int64_t c);


/**
 * Interface for 0.5.0 version
 *
 * do not even think about it's usage for this moment
 */

typedef struct {
    /// compressed size used from given memory buffer
    int size;
    /// I/P/B frame type
    int frame_type;
} avc_enc_result_t;

/**
 * Commands
 * order can't be changed - once it was defined
 */
typedef enum {
    // general commands
    AVC_OPEN_BY_NAME = 0xACA000,
    AVC_OPEN_BY_CODEC_ID,
    AVC_OPEN_BY_FOURCC,
    AVC_CLOSE,

    AVC_FLUSH,
    // pin - struct { uint8_t* src, uint_t src_size }
    // pout - struct { AVPicture* img, consumed_bytes,
    AVC_DECODE,
    // pin - struct { AVPicture* img, uint8_t* dest, uint_t dest_size }
    // pout - uint_t used_from_dest_size
    AVC_ENCODE, 

    // query/get video commands
    AVC_GET_VERSION = 0xACB000,
    AVC_GET_WIDTH,
    AVC_GET_HEIGHT,
    AVC_GET_DELAY,
    AVC_GET_QUANT_TABLE,
    // ...

    // query/get audio commands
    AVC_GET_FRAME_SIZE = 0xABC000,

    // maybe define some simple structure which
    // might be passed to the user - but they can't
    // contain any codec specific parts and these
    // calls are usualy necessary only few times

    // set video commands
    AVC_SET_WIDTH = 0xACD000,
    AVC_SET_HEIGHT,

    // set video encoding commands
    AVC_SET_FRAME_RATE = 0xACD800,
    AVC_SET_QUALITY,
    AVC_SET_HURRY_UP,

    // set audio commands
    AVC_SET_SAMPLE_RATE = 0xACE000,
    AVC_SET_CHANNELS,

} avc_cmd_t;

/**
 * \param handle  allocated private structure by libavcodec
 *                for initialization pass NULL - will be returned pout
 *                user is supposed to know nothing about its structure
 * \param cmd     type of operation to be performed
 * \param pint    input parameter
 * \param pout    output parameter
 *
 * \returns  command status - eventually for query command it might return
 * integer resulting value
 */
int avcodec(void* handle, avc_cmd_t cmd, void* pin, void* pout);

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
} AVCodecParserContext;

typedef struct AVCodecParser {
    int codec_ids[3]; /* several codec IDs are permitted */
    int priv_data_size;
    int (*parser_init)(AVCodecParserContext *s);
    int (*parser_parse)(AVCodecParserContext *s, 
                        AVCodecContext *avctx,
                        uint8_t **poutbuf, int *poutbuf_size, 
                        const uint8_t *buf, int buf_size);
    void (*parser_close)(AVCodecParserContext *s);
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
void av_parser_close(AVCodecParserContext *s);

extern AVCodecParser mpegvideo_parser;
extern AVCodecParser mpeg4video_parser;
extern AVCodecParser h261_parser;
extern AVCodecParser h263_parser;
extern AVCodecParser h264_parser;
extern AVCodecParser mpegaudio_parser;
extern AVCodecParser ac3_parser;

/* memory */
void *av_malloc(unsigned int size);
void *av_mallocz(unsigned int size);
void *av_realloc(void *ptr, unsigned int size);
void av_free(void *ptr);
char *av_strdup(const char *s);
void av_freep(void *ptr);
void *av_fast_realloc(void *ptr, unsigned int *size, unsigned int min_size);
/* for static data only */
/* call av_free_static to release all staticaly allocated tables */
void av_free_static(void);
void *av_mallocz_static(unsigned int size);

/* add by bero : in adx.c */
int is_adx(const unsigned char *buf,size_t bufsize);

void img_copy(AVPicture *dst, const AVPicture *src,
              int pix_fmt, int width, int height);

/* av_log API */

#include <stdarg.h>

#define AV_LOG_QUIET -1
#define AV_LOG_ERROR 0
#define AV_LOG_INFO 1
#define AV_LOG_DEBUG 2

#ifdef __GNUC__
extern void av_log(void*, int level, const char *fmt, ...) __attribute__ ((__format__ (__printf__, 3, 4)));
#else
extern void av_log(void*, int level, const char *fmt, ...);
#endif

extern void av_vlog(void*, int level, const char *fmt, va_list);
extern int av_log_get_level(void);
extern void av_log_set_level(int);
extern void av_log_set_callback(void (*)(void*, int, const char*, va_list));

/* endian macros */
#if !defined(BE_16) || !defined(BE_32) || !defined(LE_16) || !defined(LE_32)
#define BE_16(x)  ((((uint8_t*)(x))[0] << 8) | ((uint8_t*)(x))[1])
#define BE_32(x)  ((((uint8_t*)(x))[0] << 24) | \
                   (((uint8_t*)(x))[1] << 16) | \
                   (((uint8_t*)(x))[2] << 8) | \
                    ((uint8_t*)(x))[3])
#define LE_16(x)  ((((uint8_t*)(x))[1] << 8) | ((uint8_t*)(x))[0])
#define LE_32(x)  ((((uint8_t*)(x))[3] << 24) | \
                   (((uint8_t*)(x))[2] << 16) | \
                   (((uint8_t*)(x))[1] << 8) | \
                    ((uint8_t*)(x))[0])
#endif

#ifdef __cplusplus
}
#endif

#endif /* AVCODEC_H */
