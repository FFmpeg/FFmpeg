
#define LIBAV_VERSION_INT 0x000406  
#define LIBAV_VERSION     "0.4.6"
#define LIBAV_BUILD       4601

#include "avcodec.h"

#include "avio.h"

/* packet functions */

typedef struct AVPacket {
    INT64 pts;
    UINT8 *data;
    int size;
    int stream_index;
    int flags;
    int duration;       
#define PKT_FLAG_KEY   0x0001
#define PKT_FLAG_DROPPED_FRAME   0x0002
} AVPacket; 

int av_new_packet(AVPacket *pkt, int size);
void av_free_packet(AVPacket *pkt);

/*************************************************/
/* output formats */

struct AVFormatContext;
struct AVFormatInputContext;

typedef struct AVFormatParameters {
    int frame_rate;
    int sample_rate;
    int channels;
    int width;
    int height;
    enum PixelFormat pix_fmt;
} AVFormatParameters;

typedef struct AVFormat {
    const char *name;
    const char *long_name;
    const char *mime_type;
    const char *extensions; /* comma separated extensions */

    /* output support */
    enum CodecID audio_codec; /* default audio codec */
    enum CodecID video_codec; /* default video codec */
    int (*write_header)(struct AVFormatContext *);
    int (*write_packet)(struct AVFormatContext *, 
                        int stream_index,
                        unsigned char *buf, int size, int force_pts);
    int (*write_trailer)(struct AVFormatContext *);

    /* optional input support */
    /* read the format header and initialize the AVFormatInputContext
       structure. Return 0 if OK. 'ap' if non NULL contains
       additionnal paramters. Only used in raw format right now */
    int (*read_header)(struct AVFormatContext *,
                       AVFormatParameters *ap);
    /* read one packet and put it in 'pkt'. pts and flags are also set */
    int (*read_packet)(struct AVFormatContext *, AVPacket *pkt);
    /* close the stream. The AVFormatContext and AVStreams are not
       freed by this function */
    int (*read_close)(struct AVFormatContext *);
    /* seek at or before a given pts (given in microsecond). The pts
       origin is defined by the stream */
    int (*read_seek)(struct AVFormatContext *, INT64 pts);
    int flags;
#define AVFMT_NOFILE        0x0001 /* no file should be opened */
#define AVFMT_NEEDNUMBER    0x0002 /* needs '%d' in filename */ 
    struct AVFormat *next;
} AVFormat;

typedef struct AVStream {
    int id;       /* internal stream id */
    AVCodecContext codec; /* codec context */
    void *priv_data;
} AVStream;

#define MAX_STREAMS 20

/* format I/O context */
typedef struct AVFormatContext {
    struct AVFormat *format;
    void *priv_data;
    ByteIOContext pb;
    int nb_streams;
    AVStream *streams[MAX_STREAMS];
    char filename[1024]; /* input or output filename */
    /* stream info */
    char title[512];
    char author[512];
    char copyright[512];
    char comment[512];
    int flags; /* format specific flags */
    /* This buffer is only needed when packets were already buffered but
       not decoded, for example to get the codec parameters in mpeg
       streams */
   struct AVPacketList *packet_buffer;
} AVFormatContext;

typedef struct AVPacketList {
    AVPacket pkt;
    struct AVPacketList *next;
} AVPacketList;

extern AVFormat *first_format;

/* rv10enc.c */
extern AVFormat rm_format;

/* mpegmux.c */
#define AVF_FLAG_VCD   0x00000001   /* VCD compatible MPEG-PS */
extern AVFormat mpeg_mux_format;

/* asfenc.c */
extern AVFormat asf_format;

/* avienc.c */
extern AVFormat avi_format;

/* mov.c */
extern AVFormat mov_format;
extern AVFormat mp4_format;

/* jpegenc.c */
extern AVFormat mpjpeg_format;
extern AVFormat jpeg_format;
extern AVFormat single_jpeg_format;

/* swfenc.c */
extern AVFormat swf_format;

/* gif.c */
extern AVFormat gif_format;
/* au.c */
extern AVFormat au_format;

/* wav.c */
extern AVFormat wav_format;

/* img.c */
extern AVFormat pgm_format;
extern AVFormat ppm_format;
extern AVFormat pgmyuv_format;
extern AVFormat imgyuv_format;

extern AVFormat pgmpipe_format;
extern AVFormat pgmyuvpipe_format;
extern AVFormat ppmpipe_format;

/* raw.c */
extern AVFormat mp2_format;
extern AVFormat ac3_format;
extern AVFormat h263_format;
extern AVFormat mpeg1video_format;
extern AVFormat mjpeg_format;
extern AVFormat pcm_s16le_format;
extern AVFormat pcm_s16be_format;
extern AVFormat pcm_u16le_format;
extern AVFormat pcm_u16be_format;
extern AVFormat pcm_s8_format;
extern AVFormat pcm_u8_format;
extern AVFormat pcm_mulaw_format;
extern AVFormat pcm_alaw_format;
extern AVFormat rawvideo_format;

/* ffm.c */
extern AVFormat ffm_format;

/* formats.c */

#define MKTAG(a,b,c,d) (a | (b << 8) | (c << 16) | (d << 24))
#define MKBETAG(a,b,c,d) (d | (c << 8) | (b << 16) | (a << 24))

void register_avformat(AVFormat *format);
AVFormat *guess_format(const char *short_name, const char *filename, const char *mime_type);

int strstart(const char *str, const char *val, const char **ptr);
void nstrcpy(char *buf, int buf_size, const char *str);
/* This does what strncpy ought to do. */
void strlcpy(char *dst, const char *src, int dst_size);
int match_ext(const char *filename, const char *extensions);

void register_all(void);

INT64 gettime(void);

typedef struct FifoBuffer {
    UINT8 *buffer;
    UINT8 *rptr, *wptr, *end;
} FifoBuffer;

int fifo_init(FifoBuffer *f, int size);
void fifo_free(FifoBuffer *f);
int fifo_size(FifoBuffer *f, UINT8 *rptr);
int fifo_read(FifoBuffer *f, UINT8 *buf, int buf_size, UINT8 **rptr_ptr);
void fifo_write(FifoBuffer *f, UINT8 *buf, int size, UINT8 **wptr_ptr);

AVFormatContext *av_open_input_file(const char *filename, 
                                    const char *format_name,
                                    int buf_size,
                                    AVFormatParameters *ap);
int av_read_packet(AVFormatContext *s, AVPacket *pkt);
void av_close_input_file(AVFormatContext *s);

int av_write_packet(AVFormatContext *s, AVPacket *pkt, int force_pts);

void dump_format(AVFormatContext *ic,
                 int index, 
                 const char *url,
                 int is_output);
int parse_image_size(int *width_ptr, int *height_ptr, const char *str);
INT64 gettime(void);
INT64 parse_date(const char *datestr, int duration);

/* ffm specific for ffserver */
#define FFM_PACKET_SIZE 4096
offset_t ffm_read_write_index(int fd);
void ffm_write_write_index(int fd, offset_t pos);
void ffm_set_write_index(AVFormatContext *s, offset_t pos, offset_t file_size);

int find_info_tag(char *arg, int arg_size, const char *tag1, const char *info);

int get_frame_filename(char *buf, int buf_size,
                       const char *path, int number);

/* grab/output specific */
extern AVFormat video_grab_device_format;
extern AVFormat audio_device_format;

extern const char *v4l_device;
extern const char *audio_device;
