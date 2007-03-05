/*
 * unbuffered io for ffmpeg system
 * copyright (c) 2001 Fabrice Bellard
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */
#ifndef AVIO_H
#define AVIO_H

/* output byte stream handling */

typedef int64_t offset_t;

/* unbuffered I/O */

struct URLContext {
    struct URLProtocol *prot;
    int flags;
    int is_streamed;  /**< true if streamed (no seek possible), default = false */
    int max_packet_size;  /**< if non zero, the stream is packetized with this max packet size */
    void *priv_data;
#if LIBAVFORMAT_VERSION_INT >= (52<<16)
    char *filename; /**< specified filename */
#else
    char filename[1]; /**< specified filename */
#endif
};

typedef struct URLContext URLContext;

typedef struct URLPollEntry {
    URLContext *handle;
    int events;
    int revents;
} URLPollEntry;

#define URL_RDONLY 0
#define URL_WRONLY 1
#define URL_RDWR   2

typedef int URLInterruptCB(void);

int url_open(URLContext **h, const char *filename, int flags);
int url_read(URLContext *h, unsigned char *buf, int size);
int url_write(URLContext *h, unsigned char *buf, int size);
offset_t url_seek(URLContext *h, offset_t pos, int whence);
int url_close(URLContext *h);
int url_exist(const char *filename);
offset_t url_filesize(URLContext *h);

/**
 * Return the maximum packet size associated to packetized file
 * handle. If the file is not packetized (stream like http or file on
 * disk), then 0 is returned.
 *
 * @param h file handle
 * @return maximum packet size in bytes
 */
int url_get_max_packet_size(URLContext *h);
void url_get_filename(URLContext *h, char *buf, int buf_size);

/**
 * the callback is called in blocking functions to test regulary if
 * asynchronous interruption is needed. AVERROR(EINTR) is returned
 * in this case by the interrupted function. 'NULL' means no interrupt
 * callback is given. i
 */
void url_set_interrupt_cb(URLInterruptCB *interrupt_cb);

/* not implemented */
int url_poll(URLPollEntry *poll_table, int n, int timeout);

/**
 * passing this as the "whence" parameter to a seek function causes it to
 * return the filesize without seeking anywhere, supporting this is optional
 * if its not supprted then the seek function will return <0
 */
#define AVSEEK_SIZE 0x10000

typedef struct URLProtocol {
    const char *name;
    int (*url_open)(URLContext *h, const char *filename, int flags);
    int (*url_read)(URLContext *h, unsigned char *buf, int size);
    int (*url_write)(URLContext *h, unsigned char *buf, int size);
    offset_t (*url_seek)(URLContext *h, offset_t pos, int whence);
    int (*url_close)(URLContext *h);
    struct URLProtocol *next;
} URLProtocol;

extern URLProtocol *first_protocol;
extern URLInterruptCB *url_interrupt_cb;

int register_protocol(URLProtocol *protocol);

typedef struct {
    unsigned char *buffer;
    int buffer_size;
    unsigned char *buf_ptr, *buf_end;
    void *opaque;
    int (*read_packet)(void *opaque, uint8_t *buf, int buf_size);
    int (*write_packet)(void *opaque, uint8_t *buf, int buf_size);
    offset_t (*seek)(void *opaque, offset_t offset, int whence);
    offset_t pos; /**< position in the file of the current buffer */
    int must_flush; /**< true if the next seek should flush */
    int eof_reached; /**< true if eof reached */
    int write_flag;  /**< true if open for writing */
    int is_streamed;
    int max_packet_size;
    unsigned long checksum;
    unsigned char *checksum_ptr;
    unsigned long (*update_checksum)(unsigned long checksum, const uint8_t *buf, unsigned int size);
    int error;         ///< contains the error code or 0 if no error happened
} ByteIOContext;

int init_put_byte(ByteIOContext *s,
                  unsigned char *buffer,
                  int buffer_size,
                  int write_flag,
                  void *opaque,
                  int (*read_packet)(void *opaque, uint8_t *buf, int buf_size),
                  int (*write_packet)(void *opaque, uint8_t *buf, int buf_size),
                  offset_t (*seek)(void *opaque, offset_t offset, int whence));

void put_byte(ByteIOContext *s, int b);
void put_buffer(ByteIOContext *s, const unsigned char *buf, int size);
void put_le64(ByteIOContext *s, uint64_t val);
void put_be64(ByteIOContext *s, uint64_t val);
void put_le32(ByteIOContext *s, unsigned int val);
void put_be32(ByteIOContext *s, unsigned int val);
void put_le24(ByteIOContext *s, unsigned int val);
void put_be24(ByteIOContext *s, unsigned int val);
void put_le16(ByteIOContext *s, unsigned int val);
void put_be16(ByteIOContext *s, unsigned int val);
void put_tag(ByteIOContext *s, const char *tag);

void put_strz(ByteIOContext *s, const char *buf);

offset_t url_fseek(ByteIOContext *s, offset_t offset, int whence);
void url_fskip(ByteIOContext *s, offset_t offset);
offset_t url_ftell(ByteIOContext *s);
offset_t url_fsize(ByteIOContext *s);
int url_feof(ByteIOContext *s);
int url_ferror(ByteIOContext *s);

#define URL_EOF (-1)
/** @note return URL_EOF (-1) if EOF */
int url_fgetc(ByteIOContext *s);

/** @warning currently size is limited */
#ifdef __GNUC__
int url_fprintf(ByteIOContext *s, const char *fmt, ...) __attribute__ ((__format__ (__printf__, 2, 3)));
#else
int url_fprintf(ByteIOContext *s, const char *fmt, ...);
#endif

/** @note unlike fgets, the EOL character is not returned and a whole
   line is parsed. return NULL if first char read was EOF */
char *url_fgets(ByteIOContext *s, char *buf, int buf_size);

void put_flush_packet(ByteIOContext *s);

int get_buffer(ByteIOContext *s, unsigned char *buf, int size);
int get_partial_buffer(ByteIOContext *s, unsigned char *buf, int size);

/** @note return 0 if EOF, so you cannot use it if EOF handling is
   necessary */
int get_byte(ByteIOContext *s);
unsigned int get_le24(ByteIOContext *s);
unsigned int get_le32(ByteIOContext *s);
uint64_t get_le64(ByteIOContext *s);
unsigned int get_le16(ByteIOContext *s);

char *get_strz(ByteIOContext *s, char *buf, int maxlen);
unsigned int get_be16(ByteIOContext *s);
unsigned int get_be24(ByteIOContext *s);
unsigned int get_be32(ByteIOContext *s);
uint64_t get_be64(ByteIOContext *s);

static inline int url_is_streamed(ByteIOContext *s)
{
    return s->is_streamed;
}

int url_fdopen(ByteIOContext *s, URLContext *h);

/** @warning must be called before any I/O */
int url_setbufsize(ByteIOContext *s, int buf_size);

/** @note when opened as read/write, the buffers are only used for
   reading */
int url_fopen(ByteIOContext *s, const char *filename, int flags);
int url_fclose(ByteIOContext *s);
URLContext *url_fileno(ByteIOContext *s);

/**
 * Return the maximum packet size associated to packetized buffered file
 * handle. If the file is not packetized (stream like http or file on
 * disk), then 0 is returned.
 *
 * @param h buffered file handle
 * @return maximum packet size in bytes
 */
int url_fget_max_packet_size(ByteIOContext *s);

int url_open_buf(ByteIOContext *s, uint8_t *buf, int buf_size, int flags);

/** return the written or read size */
int url_close_buf(ByteIOContext *s);

/**
 * Open a write only memory stream.
 *
 * @param s new IO context
 * @return zero if no error.
 */
int url_open_dyn_buf(ByteIOContext *s);

/**
 * Open a write only packetized memory stream with a maximum packet
 * size of 'max_packet_size'.  The stream is stored in a memory buffer
 * with a big endian 4 byte header giving the packet size in bytes.
 *
 * @param s new IO context
 * @param max_packet_size maximum packet size (must be > 0)
 * @return zero if no error.
 */
int url_open_dyn_packet_buf(ByteIOContext *s, int max_packet_size);

/**
 * Return the written size and a pointer to the buffer. The buffer
 *  must be freed with av_free().
 * @param s IO context
 * @param pointer to a byte buffer
 * @return the length of the byte buffer
 */
int url_close_dyn_buf(ByteIOContext *s, uint8_t **pbuffer);

unsigned long get_checksum(ByteIOContext *s);
void init_checksum(ByteIOContext *s, unsigned long (*update_checksum)(unsigned long c, const uint8_t *p, unsigned int len), unsigned long checksum);

/* file.c */
extern URLProtocol file_protocol;
extern URLProtocol pipe_protocol;

/* udp.c */
extern URLProtocol udp_protocol;
int udp_set_remote_url(URLContext *h, const char *uri);
int udp_get_local_port(URLContext *h);
int udp_get_file_handle(URLContext *h);

/* tcp.c  */
extern URLProtocol tcp_protocol;

/* http.c */
extern URLProtocol http_protocol;

#endif

