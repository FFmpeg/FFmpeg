#ifndef AVFORMAT_H
#define AVFORMAT_H

#define LIBAVFORMAT_VERSION_INT 0x000406  
#define LIBAVFORMAT_VERSION     "0.4.6"
#define LIBAVFORMAT_BUILD       4602

#include "avcodec.h"

#include "avio.h"

/* packet functions */

#define AV_NOPTS_VALUE 0

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
/* input/output formats */

struct AVFormatContext;

/* this structure contains the data a format has to probe a file */
typedef struct AVProbeData {
    char *filename;
    unsigned char *buf;
    int buf_size;
} AVProbeData;

#define AVPROBE_SCORE_MAX 100

typedef struct AVFormatParameters {
    int frame_rate;
    int sample_rate;
    int channels;
    int width;
    int height;
    enum PixelFormat pix_fmt;
} AVFormatParameters;

#define AVFMT_NOFILE        0x0001 /* no file should be opened */
#define AVFMT_NEEDNUMBER    0x0002 /* needs '%d' in filename */ 
#define AVFMT_NOHEADER      0x0004 /* signal that no header is present
                                      (streams are added dynamically) */
#define AVFMT_SHOW_IDS      0x0008 /* show format stream IDs numbers */
#define AVFMT_RGB24         0x0010 /* force RGB24 output for ppm (hack
                                      - need better api) */
#define AVFMT_RAWPICTURE    0x0020 /* format wants AVPicture structure for
                                      raw picture data */

typedef struct AVOutputFormat {
    const char *name;
    const char *long_name;
    const char *mime_type;
    const char *extensions; /* comma separated extensions */
    /* size of private data so that it can be allocated in the wrapper */
    int priv_data_size;
    /* output support */
    enum CodecID audio_codec; /* default audio codec */
    enum CodecID video_codec; /* default video codec */
    int (*write_header)(struct AVFormatContext *);
    int (*write_packet)(struct AVFormatContext *, 
                        int stream_index,
                        unsigned char *buf, int size, int force_pts);
    int (*write_trailer)(struct AVFormatContext *);
    /* can use flags: AVFMT_NOFILE, AVFMT_NEEDNUMBER */
    int flags;
    /* private fields */
    struct AVOutputFormat *next;
} AVOutputFormat;

typedef struct AVInputFormat {
    const char *name;
    const char *long_name;
    /* size of private data so that it can be allocated in the wrapper */
    int priv_data_size;
    /* tell if a given file has a chance of being parsing by this format */
    int (*read_probe)(AVProbeData *);
    /* read the format header and initialize the AVFormatContext
       structure. Return 0 if OK. 'ap' if non NULL contains
       additionnal paramters. Only used in raw format right
       now. 'av_new_stream' should be called to create new streams.  */
    int (*read_header)(struct AVFormatContext *,
                       AVFormatParameters *ap);
    /* read one packet and put it in 'pkt'. pts and flags are also
       set. 'av_new_stream' can be called only if the flag
       AVFMT_NOHEADER is used. */
    int (*read_packet)(struct AVFormatContext *, AVPacket *pkt);
    /* close the stream. The AVFormatContext and AVStreams are not
       freed by this function */
    int (*read_close)(struct AVFormatContext *);
    /* seek at or before a given pts (given in microsecond). The pts
       origin is defined by the stream */
    int (*read_seek)(struct AVFormatContext *, INT64 pts);
    /* can use flags: AVFMT_NOFILE, AVFMT_NEEDNUMBER, AVFMT_NOHEADER */
    int flags;
    /* if extensions are defined, then no probe is done. You should
       usually not use extension format guessing because it is not
       reliable enough */
    const char *extensions;
    /* general purpose read only value that the format can use */
    int value;
    /* private fields */
    struct AVInputFormat *next;
} AVInputFormat;

typedef struct AVStream {
    int index;    /* stream index in AVFormatContext */
    int id;       /* format specific stream id */
    AVCodecContext codec; /* codec context */
    int r_frame_rate;     /* real frame rate of the stream */
    uint64_t time_length; /* real length of the stream in miliseconds */
    void* extra_data;     /* some extra data - i.e. longer WAVEFORMATEX */
    int extra_data_size;  /* size of extra data chunk */
    void *priv_data;
    /* internal data used in av_find_stream_info() */
    int codec_info_state;     
    int codec_info_nb_repeat_frames;
    int codec_info_nb_real_frames;
} AVStream;

#define MAX_STREAMS 20

/* format I/O context */
typedef struct AVFormatContext {
    /* can only be iformat or oformat, not both at the same time */
    struct AVInputFormat *iformat;
    struct AVOutputFormat *oformat;
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

extern AVInputFormat *first_iformat;
extern AVOutputFormat *first_oformat;

/* XXX: use automatic init with either ELF sections or C file parser */
/* modules */

/* mpeg.c */
#define AVF_FLAG_VCD   0x00000001   /* VCD compatible MPEG-PS */
int mpegps_init(void);

/* mpegts.c */
extern AVInputFormat mpegts_demux;
int mpegts_init(void);

/* rm.c */
int rm_init(void);

/* crc.c */
int crc_init(void);

/* img.c */
int img_init(void);

/* asf.c */
int asf_init(void);

/* avienc.c */
int avienc_init(void);

/* avidec.c */
int avidec_init(void);

/* swf.c */
int swf_init(void);

/* mov.c */
int mov_init(void);

/* jpeg.c */
int jpeg_init(void);

/* gif.c */
int gif_init(void);

/* au.c */
int au_init(void);

/* wav.c */
int wav_init(void);

/* raw.c */
int raw_init(void);

/* ffm.c */
int ffm_init(void);

/* rtsp.c */
extern AVInputFormat redir_demux;
int redir_open(AVFormatContext **ic_ptr, ByteIOContext *f);

#include "rtp.h"

#include "rtsp.h"

/* utils.c */
#define MKTAG(a,b,c,d) (a | (b << 8) | (c << 16) | (d << 24))
#define MKBETAG(a,b,c,d) (d | (c << 8) | (b << 16) | (a << 24))

void av_register_input_format(AVInputFormat *format);
void av_register_output_format(AVOutputFormat *format);
AVOutputFormat *guess_stream_format(const char *short_name, 
                                    const char *filename, const char *mime_type);
AVOutputFormat *guess_format(const char *short_name, 
                             const char *filename, const char *mime_type);

void av_hex_dump(UINT8 *buf, int size);

void av_register_all(void);

typedef struct FifoBuffer {
    UINT8 *buffer;
    UINT8 *rptr, *wptr, *end;
} FifoBuffer;

int fifo_init(FifoBuffer *f, int size);
void fifo_free(FifoBuffer *f);
int fifo_size(FifoBuffer *f, UINT8 *rptr);
int fifo_read(FifoBuffer *f, UINT8 *buf, int buf_size, UINT8 **rptr_ptr);
void fifo_write(FifoBuffer *f, UINT8 *buf, int size, UINT8 **wptr_ptr);

/* media file input */
AVInputFormat *av_find_input_format(const char *short_name);
AVInputFormat *av_probe_input_format(AVProbeData *pd, int is_opened);
int av_open_input_file(AVFormatContext **ic_ptr, const char *filename, 
                       AVInputFormat *fmt,
                       int buf_size,
                       AVFormatParameters *ap);

#define AVERROR_UNKNOWN     (-1)  /* unknown error */
#define AVERROR_IO          (-2)  /* i/o error */
#define AVERROR_NUMEXPECTED (-3)  /* number syntax expected in filename */
#define AVERROR_INVALIDDATA (-4)  /* invalid data found */
#define AVERROR_NOMEM       (-5)  /* not enough memory */
#define AVERROR_NOFMT       (-6)  /* unknown format */

int av_find_stream_info(AVFormatContext *ic);
int av_read_packet(AVFormatContext *s, AVPacket *pkt);
void av_close_input_file(AVFormatContext *s);
AVStream *av_new_stream(AVFormatContext *s, int id);

/* media file output */
int av_write_header(AVFormatContext *s);
int av_write_packet(AVFormatContext *s, AVPacket *pkt, int force_pts);
int av_write_trailer(AVFormatContext *s);

void dump_format(AVFormatContext *ic,
                 int index, 
                 const char *url,
                 int is_output);
int parse_image_size(int *width_ptr, int *height_ptr, const char *str);
INT64 parse_date(const char *datestr, int duration);

INT64 av_gettime(void);

/* ffm specific for ffserver */
#define FFM_PACKET_SIZE 4096
offset_t ffm_read_write_index(int fd);
void ffm_write_write_index(int fd, offset_t pos);
void ffm_set_write_index(AVFormatContext *s, offset_t pos, offset_t file_size);

int find_info_tag(char *arg, int arg_size, const char *tag1, const char *info);

int get_frame_filename(char *buf, int buf_size,
                       const char *path, int number);
int filename_number_test(const char *filename);

/* grab specific */
int video_grab_init(void);
int audio_init(void);

extern const char *v4l_device;
extern const char *audio_device;

#ifdef HAVE_AV_CONFIG_H
int strstart(const char *str, const char *val, const char **ptr);
int stristart(const char *str, const char *val, const char **ptr);
void pstrcpy(char *buf, int buf_size, const char *str);
char *pstrcat(char *buf, int buf_size, const char *s);
int match_ext(const char *filename, const char *extensions);

struct in_addr;
int resolve_host(struct in_addr *sin_addr, const char *hostname);

void url_split(char *proto, int proto_size,
               char *hostname, int hostname_size,
               int *port_ptr,
               char *path, int path_size,
               const char *url);

#endif /* HAVE_AV_CONFIG_H */

#endif /* AVFORMAT_H */
