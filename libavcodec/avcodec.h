#ifndef AVCODEC_H
#define AVCODEC_H

#include "common.h"

#define LIBAVCODEC_VERSION_INT 0x000406
#define LIBAVCODEC_VERSION     "0.4.6"
#define LIBAVCODEC_BUILD       4623
#define LIBAVCODEC_BUILD_STR   "4623"

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
    PIX_FMT_YUV410P
};

/* currently unused, may be used if 24/32 bits samples ever supported */
enum SampleFormat {
    SAMPLE_FMT_S16 = 0,         /* signed 16 bits */
};

/* in bytes */
#define AVCODEC_MAX_AUDIO_FRAME_SIZE 18432

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

/* encoding support */
/* note not everything is supported yet */

#define CODEC_FLAG_HQ     0x0001 /* high quality (non real time) encoding */
#define CODEC_FLAG_QSCALE 0x0002 /* use fixed qscale */
#define CODEC_FLAG_4MV    0x0004 /* 4 MV per MB allowed */
#define CODEC_FLAG_QPEL   0x0010 /* use qpel MC */
#define CODEC_FLAG_GMC    0x0020 /* use GMC */
#define CODEC_FLAG_TYPE   0x0040 /* fixed I/P frame type, from avctx->key_frame */
#define CODEC_FLAG_PART   0x0080 /* use data partitioning */
/* parent program gurantees that the input for b-frame containing streams is not written to 
   for at least s->max_b_frames+1 frames, if this is not set than the input will be copied */
#define CODEC_FLAG_INPUT_PRESERVED 0x0100
#define CODEC_FLAG_PASS1 0x0200  /* use internal 2pass ratecontrol in first  pass mode */
#define CODEC_FLAG_PASS2 0x0400  /* use internal 2pass ratecontrol in second pass mode */
#define CODEC_FLAG_EXTERN_HUFF 0x1000 /* use external huffman table (for mjpeg) */
#define CODEC_FLAG_GRAY  0x2000  /* only decode/encode grayscale */
#define CODEC_FLAG_EMU_EDGE 0x4000/* dont draw edges */
#define CODEC_FLAG_DR1    0x8000 /* dr1 */
#define CODEC_FLAG_NOT_TRUNCATED  0x00010000 /* input bitstream is not truncated, except before a startcode */
/* codec capabilities */

/* decoder can use draw_horiz_band callback */
#define CODEC_CAP_DRAW_HORIZ_BAND 0x0001
#define CODEC_CAP_DR1             0x0002 /* direct rendering method 1 */
/* if 'parse_only' field is true, then avcodec_parse_frame() can be
   used */
#define CODEC_CAP_PARSE_ONLY      0x0004

#define FRAME_RATE_BASE 10000

typedef struct AVCodecContext {
    int bit_rate;
    int bit_rate_tolerance; /* amount of +- bits (>0)*/
    int flags;
    int sub_id;    /* some codecs needs additionnal format info. It is
                      stored there */
    
    int me_method; /* ME algorithm used for video coding */
    
    /* extra data from parent application to codec, e.g. huffman table
       for mjpeg */
    /* the parent should allocate and free this buffer */
    void *extradata;
    int extradata_size;
    
    /* video only */
    int frame_rate; /* frames per sec multiplied by FRAME_RATE_BASE */
    int width, height;
    int aspect_ratio_info;
#define FF_ASPECT_SQUARE 1
#define FF_ASPECT_4_3_625 2
#define FF_ASPECT_4_3_525 3
#define FF_ASPECT_16_9_625 4
#define FF_ASPECT_16_9_525 5
#define FF_ASPECT_EXTENDED 15
    int gop_size; /* 0 = intra only */
    enum PixelFormat pix_fmt;  /* pixel format, see PIX_FMT_xxx */
    int repeat_pict; /* when decoding, this signal how much the picture */
                     /* must be delayed.                                */
                     /* extra_delay = (repeat_pict / 2) * (1/fps)       */
    /* if non NULL, 'draw_horiz_band' is called by the libavcodec
       decoder to draw an horizontal band. It improve cache usage. Not
       all codecs can do that. You must check the codec capabilities
       before */
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
    int key_frame;      /* true if the previous compressed frame was 
                           a key frame (intra, or seekable) */
    int pict_type;      /* picture type of the previous 
                           en/decoded frame */
/* FIXME: these should have FF_ */
#define I_TYPE 1 // Intra
#define P_TYPE 2 // Predicted
#define B_TYPE 3 // Bi-dir predicted
#define S_TYPE 4 // S(GMC)-VOP MPEG4

    int delay;          /* number of frames the decoded output 
                           will be delayed relative to the encoded input */
    uint8_t *mbskip_table; /* =1 if MB didnt change, is only valid for I/P frames 
                              stride= mb_width = (width+15)>>4 */
    
    /* encoding parameters */
    int quality;      /* quality of the previous encoded frame 
                         (between 1 (good) and 31 (bad)) 
                         this is allso used to set the quality in vbr mode
                         and the per frame quality in CODEC_FLAG_TYPE (second pass mode) */
    float qcompress;  /* amount of qscale change between easy & hard scenes (0.0-1.0)*/
    float qblur;      /* amount of qscale smoothing over time (0.0-1.0) */
    int qmin;         /* min qscale */
    int qmax;         /* max qscale */
    int max_qdiff;    /* max qscale difference between frames */
    int max_b_frames; /* maximum b frames, the output will be delayed by max_b_frames+1 relative to the input */
    float b_quant_factor;/* qscale factor between ps and b frames */
    int rc_strategy;  /* obsolete FIXME remove */
    int b_frame_strategy;

    int hurry_up;     /* when set to 1 during decoding, b frames will be skiped
                         when set to 2 idct/dequant will be skipped too */
    
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

    /* These are for PSNR calculation, if you set get_psnr to 1 */
    /* after encoding you will have the PSNR on psnr_y/cb/cr    */
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
    int misc_bits; // cbp, mb_type
    int frame_bits;
                 
    /* the following fields are ignored */
    void *opaque;   /* can be used to carry app specific stuff */
    char codec_name[32];
    enum CodecType codec_type; /* see CODEC_TYPE_xxx */
    enum CodecID codec_id; /* see CODEC_ID_xxx */
    unsigned int codec_tag;  /* codec tag, only used if unknown codec */
    
    int workaround_bugs;       /* workaround bugs in encoders which cannot be detected automatically */
    int luma_elim_threshold;
    int chroma_elim_threshold;
    int strict_std_compliance; /* strictly follow the std (MPEG4, ...) */
    float b_quant_offset;/* qscale offset between ips and b frames, not implemented yet */
    int error_resilience;
    
#ifndef MBC
#define MBC 128
#define MBR 96
#endif
#define QP_TYPE int //FIXME note xxx this might be changed to int8_t

    QP_TYPE *quant_store; /* field for communicating with external postprocessing */

    unsigned qstride;
    uint8_t *dr_buffer[3];
    int dr_stride;
    void *dr_opaque_frame;
    void (*get_buffer_callback)(struct AVCodecContext *c, int width, int height, int pict_type);

    int has_b_frames; // is 1 if the decoded stream contains b frames
    int dr_uvstride;
    int dr_ip_buffer_count;
    int block_align; /* currently only for adpcm codec in wav/avi */
    
    int parse_only; /* decoding only: if true, only parsing is done
                       (function avcodec_parse_frame()). The frame
                       data is returned. Only MPEG codecs support this now. */
    
    int mpeg_quant; /* 0-> h263 quant 1-> mpeg quant */
    
    char *stats_out; /* encoding statistics output buffer */
    char *stats_in;  /* encoding statistics input buffer (concatenated stuff from stats_out of pass1 should be placed here)*/
    float rc_qsquish;
    float rc_qmod_amp;
    int rc_qmod_freq;
    RcOverride *rc_override;
    int rc_override_count;
    char *rc_eq;
    int rc_max_rate;
    int rc_min_rate;
    int rc_buffer_size;
    float rc_buffer_aggressivity;
    float i_quant_factor;/* qscale factor between i and p frames */
    float i_quant_offset;/* qscale offset between i and p frames */
    float rc_initial_cplx;

    int aspected_width;
    int aspected_height;

    int dct_algo;
#define FF_DCT_AUTO    0
#define FF_DCT_FASTINT 1
#define FF_DCT_INT     2
#define FF_DCT_MMX     3
#define FF_DCT_MLIB    4

    long long int pts; /* timestamp in micro seconds
                          for decoding: the timestamp from the stream or 0
                          for encoding: the timestamp which will be stored in the stream
                                        if 0 then the frame_rate will be used */   

    //FIXME this should be reordered after kabis API is finished ...
    //TODO kill kabi
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
