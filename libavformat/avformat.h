#ifndef AVFORMAT_H
#define AVFORMAT_H

#ifdef __cplusplus
extern "C" {
#endif

#define LIBAVFORMAT_BUILD       4608

#define LIBAVFORMAT_VERSION_INT FFMPEG_VERSION_INT
#define LIBAVFORMAT_VERSION     FFMPEG_VERSION
#define LIBAVFORMAT_IDENT	"FFmpeg" FFMPEG_VERSION "b" AV_STRINGIFY(LIBAVFORMAT_BUILD)

#include <time.h>

#include "avcodec.h"

#include "avio.h"

/* packet functions */

#ifndef MAXINT64
#define MAXINT64 int64_t_C(0x7fffffffffffffff)
#endif

#ifndef MININT64
#define MININT64 int64_t_C(0x8000000000000000)
#endif

#define AV_NOPTS_VALUE MININT64
#define AV_TIME_BASE 1000000

typedef struct AVPacket {
    int64_t pts; /* presentation time stamp in stream units (set av_set_pts_info) */
    uint8_t *data;
    int   size;
    int   stream_index;
    int   flags;
    int   duration;
    void  (*destruct)(struct AVPacket *);
    void  *priv;
} AVPacket; 
#define PKT_FLAG_KEY   0x0001

static inline void av_init_packet(AVPacket *pkt)
{
    pkt->pts   = AV_NOPTS_VALUE;
    pkt->flags = 0;
    pkt->stream_index = 0;
}

int av_new_packet(AVPacket *pkt, int size);

/**
 * Free a packet
 *
 * @param pkt packet to free
 */
static inline void av_free_packet(AVPacket *pkt)
{
    if (pkt && pkt->destruct) {
	pkt->destruct(pkt);
    }
}

/*************************************************/
/* fractional numbers for exact pts handling */

/* the exact value of the fractional number is: 'val + num / den'. num
   is assumed to be such as 0 <= num < den */
typedef struct AVFrac {
    int64_t val, num, den; 
} AVFrac;

void av_frac_init(AVFrac *f, int64_t val, int64_t num, int64_t den);
void av_frac_add(AVFrac *f, int64_t incr);
void av_frac_set(AVFrac *f, int64_t val);

/*************************************************/
/* input/output formats */

struct AVFormatContext;

/* this structure contains the data a format has to probe a file */
typedef struct AVProbeData {
    const char *filename;
    unsigned char *buf;
    int buf_size;
} AVProbeData;

#define AVPROBE_SCORE_MAX 100

typedef struct AVFormatParameters {
    int frame_rate;
    int frame_rate_base;
    int sample_rate;
    int channels;
    int width;
    int height;
    enum PixelFormat pix_fmt;
    struct AVImageFormat *image_format;
    int channel; /* used to select dv channel */
    const char *device; /* video4linux, audio or DV device */
    const char *standard; /* tv standard, NTSC, PAL, SECAM */
} AVFormatParameters;

#define AVFMT_NOFILE        0x0001 /* no file should be opened */
#define AVFMT_NEEDNUMBER    0x0002 /* needs '%d' in filename */ 
#define AVFMT_NOHEADER      0x0004 /* signal that no header is present
                                      (streams are added dynamically) */
#define AVFMT_SHOW_IDS      0x0008 /* show format stream IDs numbers */
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
                        const uint8_t *buf, int size, int64_t pts);
    int (*write_trailer)(struct AVFormatContext *);
    /* can use flags: AVFMT_NOFILE, AVFMT_NEEDNUMBER */
    int flags;
    /* currently only used to set pixel format if not YUV420P */
    int (*set_parameters)(struct AVFormatContext *, AVFormatParameters *);
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
    int (*read_seek)(struct AVFormatContext *, int64_t pts);
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
    int r_frame_rate_base;/* real frame rate base of the stream */
    void *priv_data;
    /* internal data used in av_find_stream_info() */
    int codec_info_state;     
    int codec_info_nb_repeat_frames;
    int codec_info_nb_real_frames;
    /* PTS generation when outputing stream */
    AVFrac pts;
    /* ffmpeg.c private use */
    int stream_copy; /* if TRUE, just copy stream */
    /* quality, as it has been removed from AVCodecContext and put in AVVideoFrame
     * MN:dunno if thats the right place, for it */
    float quality; 
    /* decoding: position of the first frame of the component, in
       AV_TIME_BASE fractional seconds. */
    int64_t start_time; 
    /* decoding: duration of the stream, in AV_TIME_BASE fractional
       seconds. */
    int64_t duration;
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
    char album[512];
    int year;  /* ID3 year, 0 if none */
    int track; /* track number, 0 if none */
    char genre[32]; /* ID3 genre */

    int flags; /* format specific flags */
    /* private data for pts handling (do not modify directly) */
    int pts_wrap_bits; /* number of bits in pts (used for wrapping control) */
    int pts_num, pts_den; /* value to convert to seconds */
    /* This buffer is only needed when packets were already buffered but
       not decoded, for example to get the codec parameters in mpeg
       streams */
    struct AVPacketList *packet_buffer;

    /* decoding: position of the first frame of the component, in
       AV_TIME_BASE fractional seconds. NEVER set this value directly:
       it is deduced from the AVStream values.  */
    int64_t start_time; 
    /* decoding: duration of the stream, in AV_TIME_BASE fractional
       seconds. NEVER set this value directly: it is deduced from the
       AVStream values.  */
    int64_t duration;
    /* decoding: total file size. 0 if unknown */
    int64_t file_size;
    /* decoding: total stream bitrate in bit/s, 0 if not
       available. Never set it directly if the file_size and the
       duration are known as ffmpeg can compute it automatically. */
    int bit_rate;
} AVFormatContext;

typedef struct AVPacketList {
    AVPacket pkt;
    struct AVPacketList *next;
} AVPacketList;

extern AVInputFormat *first_iformat;
extern AVOutputFormat *first_oformat;

/* still image support */
struct AVInputImageContext;
typedef struct AVInputImageContext AVInputImageContext;

typedef struct AVImageInfo {
    enum PixelFormat pix_fmt; /* requested pixel format */
    int width; /* requested width */
    int height; /* requested height */
    int interleaved; /* image is interleaved (e.g. interleaved GIF) */
    AVPicture pict; /* returned allocated image */
} AVImageInfo;

/* AVImageFormat.flags field constants */
#define AVIMAGE_INTERLEAVED 0x0001 /* image format support interleaved output */

typedef struct AVImageFormat {
    const char *name;
    const char *extensions;
    /* tell if a given file has a chance of being parsing by this format */
    int (*img_probe)(AVProbeData *);
    /* read a whole image. 'alloc_cb' is called when the image size is
       known so that the caller can allocate the image. If 'allo_cb'
       returns non zero, then the parsing is aborted. Return '0' if
       OK. */
    int (*img_read)(ByteIOContext *, 
                    int (*alloc_cb)(void *, AVImageInfo *info), void *);
    /* write the image */
    int supported_pixel_formats; /* mask of supported formats for output */
    int (*img_write)(ByteIOContext *, AVImageInfo *);
    int flags;
    struct AVImageFormat *next;
} AVImageFormat;

void av_register_image_format(AVImageFormat *img_fmt);
AVImageFormat *av_probe_image_format(AVProbeData *pd);
AVImageFormat *guess_image_format(const char *filename);
int av_read_image(ByteIOContext *pb, const char *filename,
                  AVImageFormat *fmt,
                  int (*alloc_cb)(void *, AVImageInfo *info), void *opaque);
int av_write_image(ByteIOContext *pb, AVImageFormat *fmt, AVImageInfo *img);

extern AVImageFormat *first_image_format;

extern AVImageFormat pnm_image_format;
extern AVImageFormat pbm_image_format;
extern AVImageFormat pgm_image_format;
extern AVImageFormat ppm_image_format;
extern AVImageFormat pam_image_format;
extern AVImageFormat pgmyuv_image_format;
extern AVImageFormat yuv_image_format;
#ifdef CONFIG_ZLIB
extern AVImageFormat png_image_format;
#endif
extern AVImageFormat jpeg_image_format;
extern AVImageFormat gif_image_format;

/* XXX: use automatic init with either ELF sections or C file parser */
/* modules */

/* mpeg.c */
extern AVInputFormat mpegps_demux;
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

/* movenc.c */
int movenc_init(void);

/* flvenc.c */
int flvenc_init(void);

/* flvdec.c */
int flvdec_init(void);

/* jpeg.c */
int jpeg_init(void);

/* gif.c */
int gif_init(void);

/* au.c */
int au_init(void);

/* amr.c */
int amr_init(void);

/* wav.c */
int wav_init(void);

/* raw.c */
int raw_init(void);

/* mp3.c */
int mp3_init(void);

/* yuv4mpeg.c */
int yuv4mpeg_init(void);

/* ogg.c */
int ogg_init(void);

/* dv.c */
int dv_init(void);

/* ffm.c */
int ffm_init(void);

/* rtsp.c */
extern AVInputFormat redir_demux;
int redir_open(AVFormatContext **ic_ptr, ByteIOContext *f);

/* 4xm.c */
int fourxm_init(void);

/* psxstr.c */
int str_init(void);

/* idroq.c */
int roq_init(void);

/* ipmovie.c */
int ipmovie_init(void);

/* nut.c */
int nut_init(void);

/* wc3movie.c */
int wc3_init(void);

#include "rtp.h"

#include "rtsp.h"

/* yuv4mpeg.c */
extern AVOutputFormat yuv4mpegpipe_oformat;

/* utils.c */
void av_register_input_format(AVInputFormat *format);
void av_register_output_format(AVOutputFormat *format);
AVOutputFormat *guess_stream_format(const char *short_name, 
                                    const char *filename, const char *mime_type);
AVOutputFormat *guess_format(const char *short_name, 
                             const char *filename, const char *mime_type);

void av_hex_dump(uint8_t *buf, int size);

void av_register_all(void);

typedef struct FifoBuffer {
    uint8_t *buffer;
    uint8_t *rptr, *wptr, *end;
} FifoBuffer;

int fifo_init(FifoBuffer *f, int size);
void fifo_free(FifoBuffer *f);
int fifo_size(FifoBuffer *f, uint8_t *rptr);
int fifo_read(FifoBuffer *f, uint8_t *buf, int buf_size, uint8_t **rptr_ptr);
void fifo_write(FifoBuffer *f, uint8_t *buf, int size, uint8_t **wptr_ptr);

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
void av_set_pts_info(AVFormatContext *s, int pts_wrap_bits,
                     int pts_num, int pts_den);

/* media file output */
int av_set_parameters(AVFormatContext *s, AVFormatParameters *ap);
int av_write_header(AVFormatContext *s);
int av_write_frame(AVFormatContext *s, int stream_index, const uint8_t *buf, 
                   int size);
int av_write_trailer(AVFormatContext *s);

void dump_format(AVFormatContext *ic,
                 int index, 
                 const char *url,
                 int is_output);
int parse_image_size(int *width_ptr, int *height_ptr, const char *str);
int parse_frame_rate(int *frame_rate, int *frame_rate_base, const char *arg);
int64_t parse_date(const char *datestr, int duration);

int64_t av_gettime(void);

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

/* DV1394 */
int dv1394_init(void);

#ifdef HAVE_AV_CONFIG_H

#include "os_support.h"

int strstart(const char *str, const char *val, const char **ptr);
int stristart(const char *str, const char *val, const char **ptr);
void pstrcpy(char *buf, int buf_size, const char *str);
char *pstrcat(char *buf, int buf_size, const char *s);

void __dynarray_add(unsigned long **tab_ptr, int *nb_ptr, unsigned long elem);

#ifdef __GNUC__
#define dynarray_add(tab, nb_ptr, elem)\
do {\
    typeof(tab) _tab = (tab);\
    typeof(elem) _elem = (elem);\
    (void)sizeof(**_tab == _elem); /* check that types are compatible */\
    __dynarray_add((unsigned long **)_tab, nb_ptr, (unsigned long)_elem);\
} while(0)
#else
#define dynarray_add(tab, nb_ptr, elem)\
do {\
    __dynarray_add((unsigned long **)(tab), nb_ptr, (unsigned long)(elem));\
} while(0)
#endif

time_t mktimegm(struct tm *tm);
const char *small_strptime(const char *p, const char *fmt, 
                           struct tm *dt);

struct in_addr;
int resolve_host(struct in_addr *sin_addr, const char *hostname);

void url_split(char *proto, int proto_size,
               char *hostname, int hostname_size,
               int *port_ptr,
               char *path, int path_size,
               const char *url);

int match_ext(const char *filename, const char *extensions);

#endif /* HAVE_AV_CONFIG_H */

#ifdef __cplusplus
}
#endif

#endif /* AVFORMAT_H */
