#ifndef AVCODEC_H
#define AVCODEC_H

#include "common.h"

#define LIBAVCODEC_VERSION_INT 0x000406
#define LIBAVCODEC_VERSION     "0.4.6"
#define LIBAVCODEC_BUILD       4643
#define LIBAVCODEC_BUILD_STR   "4643"

enum CodecID {
    CODEC_ID_NONE, 
    CODEC_ID_MPEG1VIDEO,
    CODEC_ID_H263,
    CODEC_ID_RV10,
    CODEC_ID_MP2,
    CODEC_ID_MP3LAME,
    CODEC_ID_VORBIS,
    CODEC_ID_AC3,
    CODEC_ID_MJPEG,
    CODEC_ID_MJPEGB,
    CODEC_ID_MPEG4,
    CODEC_ID_RAWVIDEO,
    CODEC_ID_MSMPEG4V1,
    CODEC_ID_MSMPEG4V2,
    CODEC_ID_MSMPEG4V3,
    CODEC_ID_WMV1,
    CODEC_ID_WMV2,
    CODEC_ID_H263P,
    CODEC_ID_H263I,
    CODEC_ID_SVQ1,
    CODEC_ID_DVVIDEO,
    CODEC_ID_DVAUDIO,
    CODEC_ID_WMAV1,
    CODEC_ID_WMAV2,
    CODEC_ID_MACE3,
    CODEC_ID_MACE6,
    CODEC_ID_HUFFYUV,

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
    CODEC_ID_ADPCM_MS,
};

enum CodecType {
    CODEC_TYPE_UNKNOWN = -1,
    CODEC_TYPE_VIDEO,
    CODEC_TYPE_AUDIO,
};

enum PixelFormat {
    PIX_FMT_ANY = -1,
    PIX_FMT_YUV420P,
    PIX_FMT_YUV422,
    PIX_FMT_RGB24,
    PIX_FMT_BGR24,
    PIX_FMT_YUV422P,
    PIX_FMT_YUV444P,
    PIX_FMT_RGBA32,
    PIX_FMT_BGRA32,
    PIX_FMT_YUV410P,
    PIX_FMT_YUV411P,
    PIX_FMT_RGB565,
    PIX_FMT_RGB555,
//    PIX_FMT_RGB5551,
    PIX_FMT_BGR565,
    PIX_FMT_BGR555,
//    PIX_FMT_GBR565,
//    PIX_FMT_GBR555
};

/* currently unused, may be used if 24/32 bits samples ever supported */
enum SampleFormat {
    SAMPLE_FMT_S16 = 0,         /* signed 16 bits */
};

/* in bytes */
#define AVCODEC_MAX_AUDIO_FRAME_SIZE 131072

/**
 * Required number of zero bytes at the end of the input bitstream for decoding.
 * to avoid overreading (and possibly segfaulting)
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

/* ME algos sorted by quality */
static const int Motion_Est_QTab[] = { ME_ZERO, ME_PHODS, ME_LOG, 
                                       ME_X1, ME_EPZS, ME_FULL };


#define FF_MAX_B_FRAMES 4

/* encoding support
   these flags can be passed in AVCodecContext.flags before initing 
   Note: note not everything is supported yet 
*/

#define CODEC_FLAG_HQ     0x0001  /* brute force MB-type decission mode (slow) */
#define CODEC_FLAG_QSCALE 0x0002  /* use fixed qscale */
#define CODEC_FLAG_4MV    0x0004  /* 4 MV per MB allowed */
#define CODEC_FLAG_QPEL   0x0010  /* use qpel MC */
#define CODEC_FLAG_GMC    0x0020  /* use GMC */
#define CODEC_FLAG_PART   0x0080  /* use data partitioning */
/* parent program gurantees that the input for b-frame containing streams is not written to 
   for at least s->max_b_frames+1 frames, if this is not set than the input will be copied */
#define CODEC_FLAG_INPUT_PRESERVED 0x0100
#define CODEC_FLAG_PASS1 0x0200   /* use internal 2pass ratecontrol in first  pass mode */
#define CODEC_FLAG_PASS2 0x0400   /* use internal 2pass ratecontrol in second pass mode */
#define CODEC_FLAG_EXTERN_HUFF 0x1000 /* use external huffman table (for mjpeg) */
#define CODEC_FLAG_GRAY  0x2000   /* only decode/encode grayscale */
#define CODEC_FLAG_EMU_EDGE 0x4000/* dont draw edges */
#define CODEC_FLAG_PSNR           0x8000 /* error[?] variables will be set during encoding */
#define CODEC_FLAG_TRUNCATED  0x00010000 /* input bitstream might be truncated at a random location instead 
                                            of only at frame boundaries */
#define CODEC_FLAG_NORMALIZE_AQP  0x00020000 /* normalize adaptive quantization */
#define CODEC_FLAG_INTERLACED_DCT 0x00040000 /* use interlaced dct */
#define CODEC_FLAG_LOW_DELAY      0x00080000 /* force low delay / will fail on b frames */
#define CODEC_FLAG_ALT_SCAN       0x00100000 /* use alternate scan */

/* codec capabilities */

#define CODEC_CAP_DRAW_HORIZ_BAND 0x0001 /* decoder can use draw_horiz_band callback */
#define CODEC_CAP_DR1             0x0002 /* direct rendering method 1 */
/* if 'parse_only' field is true, then avcodec_parse_frame() can be
   used */
#define CODEC_CAP_PARSE_ONLY      0x0004
#define CODEC_CAP_TRUNCATED       0x0008

#define FRAME_RATE_BASE 10000

#define FF_COMMON_PICTURE \
    uint8_t *data[4];\
    int linesize[4];\
    /**\
     * pointer to the first allocated byte of the picture. can be used in get_buffer/release_buffer
     * this isnt used by lavc unless the default get/release_buffer() is used\
     * encoding: \
     * decoding: \
     */\
    uint8_t *base[4];\
    /**\
     * 1 -> keyframe, 0-> not\
     * encoding: set by lavc\
     * decoding: set by lavc\
     */\
    int key_frame;\
\
    /**\
     * picture type of the frame, see ?_TYPE below\
     * encoding: set by lavc for coded_picture (and set by user for input)\
     * decoding: set by lavc\
     */\
    int pict_type;\
\
    /**\
     * presentation timestamp in micro seconds (time when frame should be shown to user)\
     * if 0 then the frame_rate will be used as reference\
     * encoding: MUST be set by user\
     * decoding: set by lavc\
     */\
    long long int pts;\
\
    /**\
     * picture number in bitstream order.\
     * encoding: set by\
     * decoding: set by lavc\
     */\
    int coded_picture_number;\
    /**\
     * encoding: set by\
     * decoding: set by lavc\
     * picture number in display order.\
     */\
    int display_picture_number;\
\
    /**\
     * quality (between 1 (good) and 31 (bad)) \
     * encoding: set by lavc for coded_picture (and set by user for input)\
     * decoding: set by lavc\
     */\
    float quality; \
\
    /**\
     * buffer age (1->was last buffer and dint change, 2->..., ...).\
     * set to something large if the buffer has not been used yet \
     * encoding: unused\
     * decoding: MUST be set by get_buffer()\
     */\
    int age;\
\
    /**\
     * is this picture used as reference\
     * encoding: unused\
     * decoding: set by lavc (before get_buffer() call))\
     */\
    int reference;\
\
    /**\
     * QP table\
     * encoding: unused\
     * decoding: set by lavc\
     */\
    int8_t *qscale_table;\
    /**\
     * QP store stride\
     * encoding: unused\
     * decoding: set by lavc\
     */\
    int qstride;\
\
    /**\
     * mbskip_table[mb]>=1 if MB didnt change\
     * stride= mb_width = (width+15)>>4\
     * encoding: unused\
     * decoding: set by lavc\
     */\
    uint8_t *mbskip_table;\
\
    /**\
     * for some private data of the user\
     * encoding: unused\
     * decoding: set by user\
     */\
    void *opaque;\
\
    /**\
     * error\
     * encoding: set by lavc if flags&CODEC_FLAG_PSNR\
     * decoding: unused\
     */\
    uint64_t error[4];\

#define FF_I_TYPE 1 // Intra
#define FF_P_TYPE 2 // Predicted
#define FF_B_TYPE 3 // Bi-dir predicted
#define FF_S_TYPE 4 // S(GMC)-VOP MPEG4

typedef struct AVVideoFrame {
    FF_COMMON_PICTURE
} AVVideoFrame;

typedef struct AVCodecContext {
    /**
     * the average bitrate
     * encoding: set by user. unused for constant quantizer encoding
     * decoding: set by lavc. 0 or some bitrate if this info is available in the stream 
     */
    int bit_rate;

    /**
     * number of bits the bitstream is allowed to diverge from the reference
     *           the reference can be CBR (for CBR pass1) or VBR (for pass2)
     * encoding: set by user. unused for constant quantizer encoding
     * decoding: unused
     */
    int bit_rate_tolerance; 

    /**
     * CODEC_FLAG_*
     * encoding: set by user.
     * decoding: set by user.
     */
    int flags;

    /**
     * some codecs needs additionnal format info. It is stored here
     * encoding: set by user. 
     * decoding: set by lavc. (FIXME is this ok?)
     */
    int sub_id;

    /**
     * motion estimation algorithm used for video coding
     * encoding: MUST be set by user.
     * decoding: unused
     */
    int me_method;

    /**
     * some codecs need / can use extra-data like huffman tables
     * mjpeg: huffman tables
     * rv10: additional flags
     * mpeg4: global headers (they can be in the bitstream or here)
     * encoding: set/allocated/freed by lavc.
     * decoding: set/allocated/freed by user.
     */
    void *extradata;
    int extradata_size;
    
    /* video only */
    /**
     * frames per sec multiplied by FRAME_RATE_BASE
     * for variable fps this is the precission, so if the timestamps 
     * can be specified in msec precssion then this is 1000*FRAME_RATE_BASE
     * encoding: MUST be set by user
     * decoding: set by lavc. 0 or the frame_rate if available
     */
    int frame_rate;

    /**
     * encoding: MUST be set by user. 
     * decoding: set by user, some codecs might override / change it during playback
     */
    int width, height;
    
#define FF_ASPECT_SQUARE 1
#define FF_ASPECT_4_3_625 2
#define FF_ASPECT_4_3_525 3
#define FF_ASPECT_16_9_625 4
#define FF_ASPECT_16_9_525 5
#define FF_ASPECT_EXTENDED 15

    /**
     * the number of pictures in a group of pitures, or 0 for intra_only
     * encoding: set by user.
     * decoding: unused
     */
    int gop_size;

    /**
     * pixel format, see PIX_FMT_xxx
     * encoding: unused
     * decoding: set by lavc.
     */
    enum PixelFormat pix_fmt;
    
    int repeat_pict; /* when decoding, this signal how much the picture */
                     /* must be delayed.                                */
                     /* extra_delay = (repeat_pict / 2) * (1/fps)       */
    
    /**
     * if non NULL, 'draw_horiz_band' is called by the libavcodec
     * decoder to draw an horizontal band. It improve cache usage. Not
     * all codecs can do that. You must check the codec capabilities
     * before
     * encoding: unused
     * decoding: set by user.
     */
    void (*draw_horiz_band)(struct AVCodecContext *s,
                            UINT8 **src_ptr, int linesize,
                            int y, int width, int height);

    /* audio only */
    int sample_rate; /* samples per sec */
    int channels;
    int sample_fmt;  /* sample format, currenly unused */

    /* the following data should not be initialized */
    int frame_size;     /* in samples, initialized when calling 'init' */
    int frame_number;   /* audio or video frame number */
    int real_pict_num;  /* returns the real picture number of
                           previous encoded frame */
    
    /**
     * 1 -> keyframe, 0-> not (this if for audio only, for video, AVVideoFrame.key_frame should be used)
     * encoding: set by lavc (for the outputed bitstream, not the input frame)
     * decoding: set by lavc (for the decoded  bitstream, not the displayed frame)
     */
    int key_frame;

    /**
     * number of frames the decoded output will be delayed relative to 
     * the encoded input
     * encoding: set by lavc.
     * decoding: unused
     */
    int delay;
    
    /* encoding parameters */
    float qcompress;  /* amount of qscale change between easy & hard scenes (0.0-1.0)*/
    float qblur;      /* amount of qscale smoothing over time (0.0-1.0) */
    
    /**
     * minimum quantizer
     * encoding: set by user.
     * decoding: unused
     */
    int qmin;

    /**
     * maximum quantizer
     * encoding: set by user.
     * decoding: unused
     */
    int qmax;

    /**
     * maximum quantizer difference etween frames
     * encoding: set by user.
     * decoding: unused
     */
    int max_qdiff;

    /**
     * maximum number of b frames between non b frames
     * note: the output will be delayed by max_b_frames+1 relative to the input
     * encoding: set by user.
     * decoding: unused
     */
    int max_b_frames;

    /**
     * qscale factor between ip and b frames
     * encoding: set by user.
     * decoding: unused
     */
    float b_quant_factor;
    
    /** obsolete FIXME remove */
    int rc_strategy;
    int b_frame_strategy;

    /**
     * encoding: unused
     * decoding: set by user. 1-> skip b frames, 2-> skip idct/dequant too, 5-> skip everything except header
     */
    int hurry_up;
    
    struct AVCodec *codec;
    
    void *priv_data;

    /* The following data is for RTP friendly coding */
    /* By now only H.263/H.263+/MPEG4 coder honours this   */
    int rtp_mode;   /* 1 for activate RTP friendly-mode           */
                    /* highers numbers represent more error-prone */
                    /* enviroments, by now just "1" exist         */
    
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
    void (*rtp_callback)(void *data, int size, int packet_number); 

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
     * number of bits used for the previously encoded frame
     * encoding: set by lavc
     * decoding: - for audio - bits_per_sample
     */
    int frame_bits;
                 
    /**
     * private data of the user, can be used to carry app specific stuff
     * encoding: set by user
     * decoding: set by user
     */
    void *opaque;

    char codec_name[32];
    enum CodecType codec_type; /* see CODEC_TYPE_xxx */
    enum CodecID codec_id; /* see CODEC_ID_xxx */
    unsigned int codec_tag;  /* codec tag, only used if unknown codec */
    
    /**
     * workaround bugs in encoders which sometimes cannot be detected automatically
     * encoding: unused
     * decoding: set by user
     */
    int workaround_bugs;
#define FF_BUG_AUTODETECT       1  //autodetection
#define FF_BUG_OLD_MSMPEG4      2
#define FF_BUG_XVID_ILACE       4
#define FF_BUG_UMP4             8
#define FF_BUG_NO_PADDING       16
#define FF_BUG_AC_VLC           32
#define FF_BUG_QPEL_CHROMA      64
//#define FF_BUG_FAKE_SCALABILITY 16 //autodetection should work 100%
        
    /**
     * encoding: set by user
     * decoding: unused
     */
    int luma_elim_threshold;
    
    /**
     * encoding: set by user
     * decoding: unused
     */
    int chroma_elim_threshold;
    
    /**
     * strictly follow the std (MPEG4, ...)
     * encoding: set by user
     * decoding: unused
     */
    int strict_std_compliance;
    
    /**
     * qscale offset between ip and b frames
     * if > 0 then the last p frame quantizer will be used (q= lastp_q*factor+offset)
     * if < 0 then normal ratecontrol will be done (q= -normal_q*factor+offset)
     * encoding: set by user.
     * decoding: unused
     */
    float b_quant_offset;
    
    /**
     * error resilience higher values will detect more errors but may missdetect
     * some more or less valid parts as errors
     * encoding: unused
     * decoding: set by user
     */
    int error_resilience;
#define FF_ER_CAREFULL        1
#define FF_ER_COMPLIANT       2
#define FF_ER_AGGRESSIVE      3
#define FF_ER_VERY_AGGRESSIVE 4
    
    /**
     * called at the beginning of each frame to get a buffer for it.
     * if pic.reference is set then the frame will be read later by lavc
     * encoding: unused
     * decoding: set by lavc, user can override
     */
    int (*get_buffer)(struct AVCodecContext *c, AVVideoFrame *pic);
    
    /**
     * called to release buffers which where allocated with get_buffer.
     * a released buffer can be reused in get_buffer()
     * pic.data[*] must be set to NULL
     * encoding: unused
     * decoding: set by lavc, user can override
     */
    void (*release_buffer)(struct AVCodecContext *c, AVVideoFrame *pic);

    /**
     * is 1 if the decoded stream contains b frames, 0 otherwise
     * encoding: unused
     * decoding: set by lavc
     */
    int has_b_frames;
    
    int block_align; /* used by some WAV based audio codecs */
    
    int parse_only; /* decoding only: if true, only parsing is done
                       (function avcodec_parse_frame()). The frame
                       data is returned. Only MPEG codecs support this now. */
    
    /**
     * 0-> h263 quant 1-> mpeg quant
     * encoding: set by user.
     * decoding: unused
     */
    int mpeg_quant;
    
    /**
     * pass1 encoding statistics output buffer
     * encoding: set by lavc
     * decoding: unused
     */
    char *stats_out; /* encoding statistics output buffer */
    
    /**
     * pass2 encoding statistics input buffer.
     * concatenated stuff from stats_out of pass1 should be placed here
     * encoding: allocated/set/freed by user
     * decoding: unused
     */
    char *stats_in;
    
    /**
     * ratecontrol qmin qmax limiting method
     * 0-> clipping, 1-> use a nice continous function to limit qscale wthin qmin/qmax
     * encoding: set by user.
     * decoding: unused
     */
    float rc_qsquish;

    float rc_qmod_amp;
    int rc_qmod_freq;
    
    /**
     * ratecontrol override, see RcOverride
     * encoding: allocated/set/freed by user.
     * decoding: unused
     */
    RcOverride *rc_override;
    int rc_override_count;
    
    /**
     * rate control equation
     * encoding: set by user
     * decoding: unused
     */
    char *rc_eq;
    
    /**
     * maximum bitrate
     * encoding: set by user.
     * decoding: unused
     */
    int rc_max_rate;
    
    /**
     * minimum bitrate
     * encoding: set by user.
     * decoding: unused
     */
    int rc_min_rate;
    
    /**
     * decoder bitstream buffer size
     * encoding: set by user.
     * decoding: unused
     */
    int rc_buffer_size;
    float rc_buffer_aggressivity;

    /**
     * qscale factor between p and i frames
     * encoding: set by user.
     * decoding: unused
     */
    float i_quant_factor;
    
    /**
     * qscale offset between p and i frames
     * if > 0 then the last p frame quantizer will be used (q= lastp_q*factor+offset)
     * if < 0 then normal ratecontrol will be done (q= -normal_q*factor+offset)
     * encoding: set by user.
     * decoding: unused
     */
    float i_quant_offset;
    
    /**
     * initial complexity for pass1 ratecontrol
     * encoding: set by user.
     * decoding: unused
     */
    float rc_initial_cplx;

    /**
     * dct algorithm, see FF_DCT_* below
     * encoding: set by user
     * decoding: unused
     */
    int dct_algo;
#define FF_DCT_AUTO    0
#define FF_DCT_FASTINT 1
#define FF_DCT_INT     2
#define FF_DCT_MMX     3
#define FF_DCT_MLIB    4
#define FF_DCT_ALTIVEC 5
    
    /**
     * luminance masking (0-> disabled)
     * encoding: set by user
     * decoding: unused
     */
    float lumi_masking;
    
    /**
     * temporary complexity masking (0-> disabled)
     * encoding: set by user
     * decoding: unused
     */
    float temporal_cplx_masking;
    
    /**
     * spatial complexity masking (0-> disabled)
     * encoding: set by user
     * decoding: unused
     */
    float spatial_cplx_masking;
    
    /**
     * p block masking (0-> disabled)
     * encoding: set by user
     * decoding: unused
     */
    float p_masking;

    /**
     * darkness masking (0-> disabled)
     * encoding: set by user
     * decoding: unused
     */
    float dark_masking;
    
    /**
     * fourcc (LSB first, so "ABCD" -> ('D'<<24) + ('C'<<16) + ('B'<<8) + 'A')
     * this is used to workaround some encoder bugs
     * encoding: unused
     * decoding: set by user, will be converted to upper case by lavc during init
     */
    int fourcc;

    /**
     * idct algorithm, see FF_IDCT_* below
     * encoding: set by user
     * decoding: set by user
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

    /**
     * slice count
     * encoding: set by lavc
     * decoding: set by user (or 0)
     */
    int slice_count;
    /**
     * slice offsets in the frame in bytes
     * encoding: set/allocated by lavc
     * decoding: set/allocated by user (or NULL)
     */
    int *slice_offset;

    /**
     * error concealment flags
     * encoding: unused
     * decoding: set by user
     */
    int error_concealment;
#define FF_EC_GUESS_MVS   1
#define FF_EC_DEBLOCK     2

    /**
     * dsp_mask could be used to disable unwanted
     * CPU features (i.e. MMX, SSE. ...)
     */
     unsigned dsp_mask;

    /**
     * bits per sample/pixel from the demuxer (needed for huffyuv)
     * encoding: set by lavc
     * decoding: set by user
     */
     int bits_per_sample;
    
    /**
     * prediction method (needed for huffyuv)
     * encoding: set by user
     * decoding: unused
     */
     int prediction_method;
#define FF_PRED_LEFT   0
#define FF_PRED_PLANE  1
#define FF_PRED_MEDIAN 2
    
    /**
     * aspect ratio. (0 if unknown)
     * encoding: set by user.
     * decoding: set by lavc.
     */
    float aspect_ratio;

    /**
     * the picture in the bitstream
     * encoding: set by lavc
     * decoding: set by lavc
     */
    AVVideoFrame *coded_picture;

    /**
     * debug 
     * encoding: set by user.
     * decoding: set by user.
     */
    int debug;
#define FF_DEBUG_PICT_INFO 1
#define FF_DEBUG_RC        2
#define FF_DEBUG_BITSTREAM 4
#define FF_DEBUG_MB_TYPE   8
#define FF_DEBUG_QP        16
#define FF_DEBUG_MV        32
#define FF_DEBUG_VIS_MV    64
    
    /**
     * error
     * encoding: set by lavc if flags&CODEC_FLAG_PSNR
     * decoding: unused
     */
    uint64_t error[4];
} AVCodecContext;

typedef struct AVCodec {
    const char *name;
    int type;
    int id;
    int priv_data_size;
    int (*init)(AVCodecContext *);
    int (*encode)(AVCodecContext *, UINT8 *buf, int buf_size, void *data);
    int (*close)(AVCodecContext *);
    int (*decode)(AVCodecContext *, void *outdata, int *outdata_size,
                  UINT8 *buf, int buf_size);
    int capabilities;
    struct AVCodec *next;
} AVCodec;

/** 
 * four components are given, that's all.
 * the last component is alpha
 */
typedef struct AVPicture {
    UINT8 *data[4];
    int linesize[4];
} AVPicture;

extern AVCodec ac3_encoder;
extern AVCodec mp2_encoder;
extern AVCodec mp3lame_encoder;
extern AVCodec oggvorbis_encoder;
extern AVCodec mpeg1video_encoder;
extern AVCodec h263_encoder;
extern AVCodec h263p_encoder;
extern AVCodec rv10_encoder;
extern AVCodec mjpeg_encoder;
extern AVCodec mpeg4_encoder;
extern AVCodec msmpeg4v1_encoder;
extern AVCodec msmpeg4v2_encoder;
extern AVCodec msmpeg4v3_encoder;
extern AVCodec wmv1_encoder;
extern AVCodec wmv2_encoder;
extern AVCodec huffyuv_encoder;

extern AVCodec h263_decoder;
extern AVCodec mpeg4_decoder;
extern AVCodec msmpeg4v1_decoder;
extern AVCodec msmpeg4v2_decoder;
extern AVCodec msmpeg4v3_decoder;
extern AVCodec wmv1_decoder;
extern AVCodec wmv2_decoder;
extern AVCodec mpeg_decoder;
extern AVCodec h263i_decoder;
extern AVCodec rv10_decoder;
extern AVCodec svq1_decoder;
extern AVCodec dvvideo_decoder;
extern AVCodec dvaudio_decoder;
extern AVCodec wmav1_decoder;
extern AVCodec wmav2_decoder;
extern AVCodec mjpeg_decoder;
extern AVCodec mjpegb_decoder;
extern AVCodec mp2_decoder;
extern AVCodec mp3_decoder;
extern AVCodec mace3_decoder;
extern AVCodec mace6_decoder;
extern AVCodec huffyuv_decoder;
extern AVCodec oggvorbis_decoder;

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
PCM_CODEC(CODEC_ID_ADPCM_MS, adpcm_ms);

#undef PCM_CODEC

/* dummy raw video codec */
extern AVCodec rawvideo_codec;

/* the following codecs use external GPL libs */
extern AVCodec ac3_decoder;

/* resample.c */

struct ReSampleContext;

typedef struct ReSampleContext ReSampleContext;

ReSampleContext *audio_resample_init(int output_channels, int input_channels, 
                                     int output_rate, int input_rate);
int audio_resample(ReSampleContext *s, short *output, short *input, int nb_samples);
void audio_resample_close(ReSampleContext *s);

/* YUV420 format is assumed ! */

struct ImgReSampleContext;

typedef struct ImgReSampleContext ImgReSampleContext;

ImgReSampleContext *img_resample_init(int output_width, int output_height,
                                      int input_width, int input_height);

ImgReSampleContext *img_resample_full_init(int owidth, int oheight,
                                      int iwidth, int iheight,
                                      int topBand, int bottomBand,
                                      int leftBand, int rightBand);

void img_resample(ImgReSampleContext *s, 
                  AVPicture *output, AVPicture *input);

void img_resample_close(ImgReSampleContext *s);

void avpicture_fill(AVPicture *picture, UINT8 *ptr,
                    int pix_fmt, int width, int height);
int avpicture_get_size(int pix_fmt, int width, int height);
void avcodec_get_chroma_sub_sample(int fmt, int *h_shift, int *v_shift);

/* convert among pixel formats */
int img_convert(AVPicture *dst, int dst_pix_fmt,
                AVPicture *src, int pix_fmt, 
                int width, int height);

/* deinterlace a picture */
int avpicture_deinterlace(AVPicture *dst, AVPicture *src,
                          int pix_fmt, int width, int height);

/* external high level API */

extern AVCodec *first_avcodec;

/* returns LIBAVCODEC_VERSION_INT constant */
unsigned avcodec_version(void);
/* returns LIBAVCODEC_BUILD constant */
unsigned avcodec_build(void);
void avcodec_init(void);

void avcodec_set_bit_exact(void);

void register_avcodec(AVCodec *format);
AVCodec *avcodec_find_encoder(enum CodecID id);
AVCodec *avcodec_find_encoder_by_name(const char *name);
AVCodec *avcodec_find_decoder(enum CodecID id);
AVCodec *avcodec_find_decoder_by_name(const char *name);
void avcodec_string(char *buf, int buf_size, AVCodecContext *enc, int encode);

void avcodec_get_context_defaults(AVCodecContext *s);
AVCodecContext *avcodec_alloc_context(void);
AVVideoFrame *avcodec_alloc_picture(void);

int avcodec_default_get_buffer(AVCodecContext *s, AVVideoFrame *pic);
void avcodec_default_release_buffer(AVCodecContext *s, AVVideoFrame *pic);

int avcodec_open(AVCodecContext *avctx, AVCodec *codec);
int avcodec_decode_audio(AVCodecContext *avctx, INT16 *samples, 
                         int *frame_size_ptr,
                         UINT8 *buf, int buf_size);
int avcodec_decode_video(AVCodecContext *avctx, AVVideoFrame *picture, 
                         int *got_picture_ptr,
                         UINT8 *buf, int buf_size);
int avcodec_parse_frame(AVCodecContext *avctx, UINT8 **pdata, 
                        int *data_size_ptr,
                        UINT8 *buf, int buf_size);
int avcodec_encode_audio(AVCodecContext *avctx, UINT8 *buf, int buf_size, 
                         const short *samples);
int avcodec_encode_video(AVCodecContext *avctx, UINT8 *buf, int buf_size, 
                         const AVVideoFrame *pict);

int avcodec_close(AVCodecContext *avctx);

void avcodec_register_all(void);

void avcodec_flush_buffers(AVCodecContext *avctx);

/**
 * Interface for 0.5.0 version
 *
 * do not even think about it's usage for this moment
 */

typedef struct {
    // compressed size used from given memory buffer
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

/* memory */
void *av_malloc(unsigned int size);
void *av_mallocz(unsigned int size);
void av_free(void *ptr);
void __av_freep(void **ptr);
#define av_freep(p) __av_freep((void **)(p))
/* for static data only */
/* call av_free_static to release all staticaly allocated tables */
void av_free_static();
void *__av_mallocz_static(void** location, unsigned int size);
#define av_mallocz_static(p, s) __av_mallocz_static((void **)(p), s)

#endif /* AVCODEC_H */
