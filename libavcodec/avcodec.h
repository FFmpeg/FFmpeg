#ifndef AVCODEC_H
#define AVCODEC_H

#include "common.h"

#define LIBAVCODEC_VERSION_INT 0x000406
#define LIBAVCODEC_VERSION     "0.4.6"
#define LIBAVCODEC_BUILD       4632
#define LIBAVCODEC_BUILD_STR   "4632"

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
#define CODEC_ID_MSMPEG4 CODEC_ID_MSMPEG4V3

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
    PIX_FMT_YUV411P
};

/* currently unused, may be used if 24/32 bits samples ever supported */
enum SampleFormat {
    SAMPLE_FMT_S16 = 0,         /* signed 16 bits */
};

/* in bytes */
#define AVCODEC_MAX_AUDIO_FRAME_SIZE 131072

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
#define CODEC_FLAG_TYPE   0x0040  /* fixed I/P frame type, from avctx->key_frame */
#define CODEC_FLAG_PART   0x0080  /* use data partitioning */
/* parent program gurantees that the input for b-frame containing streams is not written to 
   for at least s->max_b_frames+1 frames, if this is not set than the input will be copied */
#define CODEC_FLAG_INPUT_PRESERVED 0x0100
#define CODEC_FLAG_PASS1 0x0200   /* use internal 2pass ratecontrol in first  pass mode */
#define CODEC_FLAG_PASS2 0x0400   /* use internal 2pass ratecontrol in second pass mode */
#define CODEC_FLAG_EXTERN_HUFF 0x1000 /* use external huffman table (for mjpeg) */
#define CODEC_FLAG_GRAY  0x2000   /* only decode/encode grayscale */
#define CODEC_FLAG_EMU_EDGE 0x4000/* dont draw edges */
#define CODEC_FLAG_DR1    0x8000  /* direct renderig type 1 (store internal frames in external buffers) */
#define CODEC_FLAG_NOT_TRUNCATED  0x00010000 /* input bitstream is not truncated, except before a startcode 
                                                allows the last part of a frame to be decoded earlier */
#define CODEC_FLAG_NORMALIZE_AQP  0x00020000 /* normalize adaptive quantization */
#define CODEC_FLAG_INTERLACED_DCT 0x00040000 /* use interlaced dct */

/* codec capabilities */

#define CODEC_CAP_DRAW_HORIZ_BAND 0x0001 /* decoder can use draw_horiz_band callback */
#define CODEC_CAP_DR1             0x0002 /* direct rendering method 1 */
/* if 'parse_only' field is true, then avcodec_parse_frame() can be
   used */
#define CODEC_CAP_PARSE_ONLY      0x0004

#define FRAME_RATE_BASE 10000

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
     * encoding: set by user.
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
     * encoding: set by user
     * decoding: set by lavc. 0 or the frame_rate if available
     */
    int frame_rate;

    /**
     * encoding: set by user.
     * decoding: set by user, some codecs might override / change it during playback
     */
    int width, height;
    
    /**
     * encoding: set by user. 0 if not known
     * decoding: set by lavc. 0 if not known
     */
    int aspect_ratio_info;
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
     * 1 -> keyframe, 0-> not
     * encoding: set by lavc (for the outputed bitstream, not the input frame)
     * decoding: set by lavc (for the decoded  bitstream, not the displayed frame)
     */
    int key_frame;

    /**
     * picture type of the previous en/decoded frame, see ?_TYPE below
     * encoding: set by lavc (for the outputed bitstream, not the input frame)
     * decoding: set by lavc (for the decoded  bitstream, not the displayed frame)
     */
    int pict_type;
/* FIXME: these should have FF_ */
#define I_TYPE 1 // Intra
#define P_TYPE 2 // Predicted
#define B_TYPE 3 // Bi-dir predicted
#define S_TYPE 4 // S(GMC)-VOP MPEG4

    /**
     * number of frames the decoded output will be delayed relative to 
     * the encoded input
     * encoding: set by lavc.
     * decoding: unused
     */
    int delay;

    /**
     * mbskip_table[mb]=1 if MB didnt change, is only valid for I/P frames 
     * stride= mb_width = (width+15)>>4 (FIXME export stride?)
     * encoding: unused
     * decoding: set by lavc
     */
    uint8_t *mbskip_table;
    
    /* encoding parameters */
    /**
     * quality (between 1 (good) and 31 (bad)) 
     * encoding: set by user if CODEC_FLAG_QSCALE is set otherwise set by lavc
     * decoding: set by lavc
     */
    int quality;      /* quality of the previous encoded frame 
                         
                         this is allso used to set the quality in vbr mode
                         and the per frame quality in CODEC_FLAG_TYPE (second pass mode) */
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
     * decoding: set by user. 1-> skip b frames, 2-> skip idct/dequant too
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

    /**
     * if you set get_psnr to 1 then after encoding you will have the 
     * PSNR on psnr_y/cb/cr
     * encoding: set by user (1-> on, 0-> off)
     * decoding: unused
     */
    int get_psnr;
    float psnr_y;
    float psnr_cb;
    float psnr_cr;
    
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
     * decoding: unused
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
     * error resilience {-1,0,1} higher values will detect more errors but may missdetect
     * some more or less valid parts as errors
     * encoding: unused
     * decoding: set by user
     */
    int error_resilience;
    
#ifndef MBC
#define MBC 128
#define MBR 96
#endif
#define QP_TYPE int //FIXME note xxx this might be changed to int8_t

    QP_TYPE *quant_store; /* field for communicating with external postprocessing */

    unsigned qstride;
    
    /**
     * buffer, where the next picture should be decoded into
     * encoding: unused
     * decoding: set by user in get_buffer_callback to a buffer into which the next part
     *           of the bitstream will be decoded, and set by lavc at end of frame to the
     *           next frame which needs to be displayed
     */
    uint8_t *dr_buffer[3];
    
    /**
     * stride of the luminance part of the dr buffer
     * encoding: unused
     * decoding: set by user
     */
    int dr_stride;
    
    /**
     * same behavior as dr_buffer, just for some private data of the user
     * encoding: unused
     * decoding: set by user in get_buffer_callback, and set by lavc at end of frame
     */
    void *dr_opaque_frame;
    
    /**
     * called at the beginning of each frame to get a buffer for it
     * encoding: unused
     * decoding: set by user
     */
    int (*get_buffer_callback)(struct AVCodecContext *c, int width, int height, int pict_type);

    /**
     * is 1 if the decoded stream contains b frames, 0 otherwise
     * encoding: unused
     * decoding: set by lavc
     */
    int has_b_frames;

    /**
     * stride of the chrominance part of the dr buffer
     * encoding: unused
     * decoding: set by user
     */
    int dr_uvstride;
    
    /**
     * number of dr buffers
     * encoding: unused
     * decoding: set by user
     */
    int dr_ip_buffer_count;
    
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
     * custom aspect ratio, used if aspect_info==FF_ASPECT_EXTENDED
     * encoding: set by user.
     * decoding: set by lavc.
     */
    int aspected_width;
    int aspected_height;

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

    /**
     * presentation timestamp in micro seconds (time when frame should be shown to user)
     * if 0 then the frame_rate will be used as reference
     * encoding: set by user
     * decoding; set by lavc
     */
    long long int pts;
    
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

    //FIXME this should be reordered after kabis API is finished ...
    //TODO kill kabi
    /*
	Note: Below are located reserved fields for further usage
	It requires for ABI !!!
	If you'll perform some changes then borrow new space from these fields
	(void * can be safety replaced with struct * ;)
	P L E A S E ! ! !
	Note: use avcodec_alloc_context instead of malloc to allocate this, 
        otherwise the ABI compatibility will be broken between versions
 	IMPORTANT: Never change order of already declared fields!!!
     */
     //TODO: remove mess below
    unsigned long long int
	    ull_res0,ull_res1,ull_res2,ull_res3,ull_res4,ull_res5,
	    ull_res6,ull_res7,ull_res8,ull_res9,ull_res10,ull_res11;
    float
	    flt_res0,flt_res1,flt_res2,flt_res3,flt_res4,flt_res5,
	    flt_res6,flt_res7,flt_res8,flt_res9,flt_res10,flt_res11,flt_res12;
    void
	    *ptr_res0,*ptr_res1,*ptr_res2,*ptr_res3,*ptr_res4,*ptr_res5,
            *ptr_res6,*ptr_res7,*ptr_res8,*ptr_res9,*ptr_res10,*ptr_res11,*ptr_res12;
    unsigned long int
	    ul_res0,ul_res1,ul_res2,ul_res3,ul_res4,ul_res5,
	    ul_res6,ul_res7,ul_res8,ul_res9,ul_res10,ul_res11,ul_res12;
    unsigned short int
	    us_res0,us_res1,us_res2,us_res3,us_res4,us_res5,
	    us_res6,us_res7,us_res8,us_res9,us_res10,us_res11,us_res12;
    unsigned char
	    uc_res0,uc_res1,uc_res2,uc_res3,uc_res4,uc_res5,
	    uc_res6,uc_res7,uc_res8,uc_res9,uc_res10,uc_res11,uc_res12;
    unsigned int
	    ui_res0,ui_res1,ui_res2,ui_res3,ui_res4,ui_res5,ui_res6,ui_res7,ui_res8,ui_res9,
	    ui_res10,ui_res11,ui_res12,ui_res13,ui_res14,ui_res15,ui_res16;
} AVCodecContext;

typedef struct AVCodec {
    char *name;
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
    /*
	Note: Below are located reserved fields for further usage
	It requires for ABI !!!
	If you'll perform some changes then borrow new space from these fields
	(void * can be safety replaced with struct * ;)
	P L E A S E ! ! !
	IMPORTANT: Never change order of already declared fields!!!
    */
    unsigned long long int
	    ull_res0,ull_res1,ull_res2,ull_res3,ull_res4,ull_res5,
	    ull_res6,ull_res7,ull_res8,ull_res9,ull_res10,ull_res11,ull_res12;
    float
	    flt_res0,flt_res1,flt_res2,flt_res3,flt_res4,flt_res5,
	    flt_res6,flt_res7,flt_res8,flt_res9,flt_res10,flt_res11,flt_res12;
    void
	    *ptr_res0,*ptr_res1,*ptr_res2,*ptr_res3,*ptr_res4,*ptr_res5,
	    *ptr_res6,*ptr_res7,*ptr_res8,*ptr_res9,*ptr_res10,*ptr_res11,*ptr_res12;
} AVCodec;

/* three components are given, that's all */
typedef struct AVPicture {
    UINT8 *data[3];
    int linesize[3];
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
extern AVCodec mp2_decoder;
extern AVCodec mp3_decoder;

/* pcm codecs */
#define PCM_CODEC(id, name) \
extern AVCodec name ## _decoder; \
extern AVCodec name ## _encoder;

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

AVCodecContext *avcodec_alloc_context(void);
int avcodec_open(AVCodecContext *avctx, AVCodec *codec);
int avcodec_decode_audio(AVCodecContext *avctx, INT16 *samples, 
                         int *frame_size_ptr,
                         UINT8 *buf, int buf_size);
int avcodec_decode_video(AVCodecContext *avctx, AVPicture *picture, 
                         int *got_picture_ptr,
                         UINT8 *buf, int buf_size);
int avcodec_parse_frame(AVCodecContext *avctx, UINT8 **pdata, 
                        int *data_size_ptr,
                        UINT8 *buf, int buf_size);
int avcodec_encode_audio(AVCodecContext *avctx, UINT8 *buf, int buf_size, 
                         const short *samples);
int avcodec_encode_video(AVCodecContext *avctx, UINT8 *buf, int buf_size, 
                         const AVPicture *pict);

int avcodec_close(AVCodecContext *avctx);

void avcodec_register_all(void);

void avcodec_flush_buffers(AVCodecContext *avctx);

#ifdef FF_POSTPROCESS
extern int quant_store[MBR+1][MBC+1]; // [Review]
#endif


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
void *av_malloc(int size);
void *av_mallocz(int size);
void av_free(void *ptr);
void __av_freep(void **ptr);
#define av_freep(p) __av_freep((void **)(p))

#endif /* AVCODEC_H */
