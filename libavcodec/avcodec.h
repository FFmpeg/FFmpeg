#include "common.h"

enum CodecID {
    CODEC_ID_NONE, 
    CODEC_ID_MPEG1VIDEO,
    CODEC_ID_H263,
    CODEC_ID_RV10,
    CODEC_ID_MP2,
    CODEC_ID_AC3,
    CODEC_ID_MJPEG,
    CODEC_ID_OPENDIVX,
    CODEC_ID_PCM,
    CODEC_ID_RAWVIDEO,
    CODEC_ID_MSMPEG4,
    CODEC_ID_H263P,
    CODEC_ID_H263I,
};

enum CodecType {
    CODEC_TYPE_VIDEO,
    CODEC_TYPE_AUDIO,
};

enum PixelFormat {
    PIX_FMT_YUV420P,
    PIX_FMT_YUV422,
    PIX_FMT_RGB24,
    PIX_FMT_BGR24,
    PIX_FMT_YUV422P,
    PIX_FMT_YUV444P,
};

/* in bytes */
#define AVCODEC_MAX_AUDIO_FRAME_SIZE 18432

/* motion estimation type */
extern int motion_estimation_method;
#define ME_ZERO   0
#define ME_FULL   1
#define ME_LOG    2
#define ME_PHODS  3

/* encoding support */

#define CODEC_FLAG_HQ     0x0001 /* high quality (non real time) encoding */
#define CODEC_FLAG_QSCALE 0x0002 /* use fixed qscale */

#define FRAME_RATE_BASE 10000

typedef struct AVCodecContext {
    int bit_rate;
    int flags;
    int sub_id;    /* some codecs needs additionnal format info. It is
                      stored there */
    /* video only */
    int frame_rate; /* frames per sec multiplied by FRAME_RATE_BASE */
    int width, height;
    int gop_size; /* 0 = intra only */
    int pix_fmt;  /* pixel format, see PIX_FMT_xxx */
    /* audio only */
    int sample_rate; /* samples per sec */
    int channels;

    /* the following data should not be initialized */
    int frame_size; /* in samples, initialized when calling 'init' */
    int frame_number; /* audio or video frame number */
    int key_frame;    /* true if the previous compressed frame was 
                         a key frame (intra, or seekable) */
    int quality;      /* quality of the previous encoded frame 
                         (between 1 (good) and 31 (bad)) */
    struct AVCodec *codec;
    void *priv_data;

    /* the following fields are ignored */
    char codec_name[32];
    int codec_type; /* see CODEC_TYPE_xxx */
    int codec_id; /* see CODEC_ID_xxx */
    unsigned int codec_tag;  /* codec tag, only used if unknown codec */
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
    struct AVCodec *next;
} AVCodec;

/* three components are given, that's all */
typedef struct AVPicture {
    UINT8 *data[3];
    int linesize[3];
} AVPicture;

extern AVCodec ac3_encoder;
extern AVCodec mp2_encoder;
extern AVCodec mpeg1video_encoder;
extern AVCodec h263_encoder;
extern AVCodec h263p_encoder;
extern AVCodec rv10_encoder;
extern AVCodec mjpeg_encoder;
extern AVCodec opendivx_encoder;
extern AVCodec msmpeg4_encoder;

extern AVCodec h263_decoder;
extern AVCodec opendivx_decoder;
extern AVCodec msmpeg4_decoder;
extern AVCodec mpeg_decoder;
extern AVCodec h263i_decoder;
extern AVCodec rv10_decoder;
extern AVCodec mjpeg_decoder;

/* dummy raw codecs */
extern AVCodec pcm_codec;
extern AVCodec rawvideo_codec;

/* the following codecs use external GPL libs */
extern AVCodec mp3_decoder;
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
void img_resample(ImgReSampleContext *s, 
                  AVPicture *output, AVPicture *input);

void img_resample_close(ImgReSampleContext *s);

int img_convert_to_yuv420(UINT8 *img_out, UINT8 *img, 
                          int pix_fmt, int width, int height);

/* external high level API */

extern AVCodec *first_avcodec;

void avcodec_init(void);

void register_avcodec(AVCodec *format);
AVCodec *avcodec_find_encoder(enum CodecID id);
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
int avcodec_encode_audio(AVCodecContext *avctx, UINT8 *buf, int buf_size, 
                         const short *samples);
int avcodec_encode_video(AVCodecContext *avctx, UINT8 *buf, int buf_size, 
                         const AVPicture *pict);

int avcodec_close(AVCodecContext *avctx);

void avcodec_register_all(void);
