/* output byte stream handling */

typedef long long offset_t;

/* unbuffered I/O */

struct URLContext {
    struct URLProtocol *prot;
    int flags;        
    int is_streamed;  /* true if streamed (no seek possible), default = false */
    int packet_size;
    void *priv_data;
};

typedef struct URLFormat {
    char format_name[32];
    int sample_rate;
    int frame_rate;
    int channels;
    int height;
    int width;
    int pix_fmt;
} URLFormat;

typedef struct URLContext URLContext;

typedef struct URLPollEntry {
    URLContext *handle;
    int events;
    int revents;
} URLPollEntry;

#define URL_RDONLY 0
#define URL_WRONLY 1
int url_open(URLContext **h, const char *filename, int flags);
int url_read(URLContext *h, unsigned char *buf, int size);
int url_write(URLContext *h, unsigned char *buf, int size);
offset_t url_seek(URLContext *h, offset_t pos, int whence);
int url_getformat(URLContext *h, URLFormat *f);
int url_close(URLContext *h);
int url_exist(const char *filename);
offset_t url_filesize(URLContext *h);
/* not implemented */
int url_poll(URLPollEntry *poll_table, int n, int timeout);

typedef struct URLProtocol {
    const char *name;
    int (*url_open)(URLContext *h, const char *filename, int flags);
    int (*url_read)(URLContext *h, unsigned char *buf, int size);
    int (*url_write)(URLContext *h, unsigned char *buf, int size);
    offset_t (*url_seek)(URLContext *h, offset_t pos, int whence);
    int (*url_close)(URLContext *h);
    /* get precise information about the format, if available. return
       -ENODATA if not available */
    int (*url_getformat)(URLContext *h, URLFormat *f);
    struct URLProtocol *next;
} URLProtocol;

extern URLProtocol *first_protocol;

int register_protocol(URLProtocol *protocol);

typedef struct {
    unsigned char *buffer;
    int buffer_size;
    unsigned char *buf_ptr, *buf_end;
    void *opaque;
    int (*read_packet)(void *opaque, UINT8 *buf, int buf_size);
    void (*write_packet)(void *opaque, UINT8 *buf, int buf_size);
    int (*seek)(void *opaque, offset_t offset, int whence);
    offset_t pos; /* position in the file of the current buffer */
    int must_flush; /* true if the next seek should flush */
    int eof_reached; /* true if eof reached */
    int write_flag;  /* true if open for writing */
    int is_streamed;
    int packet_size;
} ByteIOContext;

int init_put_byte(ByteIOContext *s,
                  unsigned char *buffer,
                  int buffer_size,
                  int write_flag,
                  void *opaque,
                  int (*read_packet)(void *opaque, UINT8 *buf, int buf_size),
                  void (*write_packet)(void *opaque, UINT8 *buf, int buf_size),
                  int (*seek)(void *opaque, offset_t offset, int whence));

void put_byte(ByteIOContext *s, int b);
void put_buffer(ByteIOContext *s, unsigned char *buf, int size);
void put_le64(ByteIOContext *s, unsigned long long val);
void put_be64(ByteIOContext *s, unsigned long long val);
void put_le32(ByteIOContext *s, unsigned int val);
void put_be32(ByteIOContext *s, unsigned int val);
void put_le16(ByteIOContext *s, unsigned int val);
void put_be16(ByteIOContext *s, unsigned int val);
void put_tag(ByteIOContext *s, char *tag);

offset_t url_fseek(ByteIOContext *s, offset_t offset, int whence);
void url_fskip(ByteIOContext *s, offset_t offset);
offset_t url_ftell(ByteIOContext *s);
int url_feof(ByteIOContext *s);

void put_flush_packet(ByteIOContext *s);

int get_buffer(ByteIOContext *s, unsigned char *buf, int size);
int get_byte(ByteIOContext *s);
unsigned int get_le32(ByteIOContext *s);
unsigned long long get_le64(ByteIOContext *s);
unsigned int get_le16(ByteIOContext *s);

unsigned int get_be16(ByteIOContext *s);
unsigned int get_be32(ByteIOContext *s);
unsigned long long get_be64(ByteIOContext *s);

extern inline int url_is_streamed(ByteIOContext *s)
{
    return s->is_streamed;
}
/* get the prefered packet size of the device. All I/Os should be done
   by multiple of this size */
extern inline int url_get_packet_size(ByteIOContext *s)
{
    return s->packet_size;
}

int url_fdopen(ByteIOContext *s, URLContext *h);
int url_setbufsize(ByteIOContext *s, int buf_size);
int url_fopen(ByteIOContext *s, const char *filename, int flags);
int url_fclose(ByteIOContext *s);
URLContext *url_fileno(ByteIOContext *s);

int url_open_buf(ByteIOContext *s, UINT8 *buf, int buf_size, int flags);
int url_close_buf(ByteIOContext *s);

/* file.c */
extern URLProtocol file_protocol;
extern URLProtocol pipe_protocol;

/* udp.c */
extern URLProtocol udp_protocol;

/* http.c */
extern URLProtocol http_protocol;

/* audio.c */
extern const char *audio_device;
extern URLProtocol audio_protocol;

/* grab.c */
extern const char *v4l_device;
extern URLProtocol video_protocol;
