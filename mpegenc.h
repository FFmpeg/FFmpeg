
#include "avcodec.h"

/* byte stream handling */

typedef struct {
    unsigned char *buffer;
    unsigned char *buf_ptr, *buf_end;
    void *opaque;
    void (*write_packet)(void *opaque, UINT8 *buf, int buf_size);
    int (*write_seek)(void *opaque, long long offset, int whence);
    long long pos; /* position in the file of the current buffer */
} PutByteContext;

int init_put_byte(PutByteContext *s,
                  unsigned char *buffer,
                  int buffer_size,
                  void *opaque,
                  void (*write_packet)(void *opaque, UINT8 *buf, int buf_size),
                  int (*write_seek)(void *opaque, long long offset, int whence));

void put_byte(PutByteContext *s, int b);
void put_buffer(PutByteContext *s, unsigned char *buf, int size);
void put_le32(PutByteContext *s, unsigned int val);
void put_le64(PutByteContext *s, unsigned long long val);
void put_le16(PutByteContext *s, unsigned int val);
void put_tag(PutByteContext *s, char *tag);

long long put_seek(PutByteContext *s, long long offset, int whence);
long long put_pos(PutByteContext *s);

void put_flush_packet(PutByteContext *s);

/* udp.c */

typedef struct {
    int udp_socket;
    int max_payload_size; /* in bytes */
} UDPContext;

int udp_tx_open(UDPContext *s,
                const char *uri,
                int local_port);
void udp_tx_close(UDPContext *s);
void udp_write_data(void *opaque, UINT8 *buf, int size);

/* generic functions */

struct AVFormatContext;

typedef struct AVFormat {
    char *name;
    char *long_name;
    char *mime_type;
    char *extensions; /* comma separated extensions */
    enum CodecID audio_codec;
    enum CodecID video_codec;
    int (*write_header)(struct AVFormatContext *);
    int (*write_audio_frame)(struct AVFormatContext *, 
                             unsigned char *buf, int size);
    int (*write_video_picture)(struct AVFormatContext *, 
                               unsigned char *buf, int size);
    int (*write_trailer)(struct AVFormatContext *);
    struct AVFormat *next;
} AVFormat;

typedef struct AVFormatContext {
    struct AVFormat *format;
    void *priv_data;
    PutByteContext pb;
    AVEncodeContext *video_enc;
    AVEncodeContext *audio_enc;
    int is_streamed; /* true if the stream is generated as being streamed */
} AVFormatContext;

extern AVFormat *first_format;
extern int data_out_size;
extern const char *comment_string;

/* rv10enc.c */
extern AVFormat rm_format;
extern AVFormat ra_format;

/* mpegmux.c */
extern AVFormat mpeg_mux_format;

/* asfenc.c */
extern AVFormat asf_format;

/* jpegenc.c */
extern AVFormat mpjpeg_format;
extern AVFormat jpeg_format;

/* swfenc.c */
extern AVFormat swf_format;

/* formats.c */
void register_avformat(AVFormat *format);
AVFormat *guess_format(const char *short_name, const char *filename, const char *mime_type);

void register_avencoder(AVEncoder *format);
AVEncoder *avencoder_find(enum CodecID id);
void avencoder_string(char *buf, int buf_size, AVEncodeContext *enc);

int avencoder_open(AVEncodeContext *avctx, AVEncoder *codec);
int avencoder_encode(AVEncodeContext *avctx, UINT8 *buf, int buf_size, void *data);
int avencoder_close(AVEncodeContext *avctx);

extern AVFormat mp2_format;
extern AVFormat ac3_format;
extern AVFormat h263_format;
extern AVFormat mpeg1video_format;

int strstart(const char *str, const char *val, const char **ptr);


/* grab.c */

extern const char *v4l_device;

long long gettime(void);
int v4l_init(int rate, int width, int height);
int v4l_read_picture(UINT8 *picture[3],
                     int width, int height,
                     int picture_number);

int audio_open(int freq, int channels);
