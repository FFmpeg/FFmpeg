/*
 * Buffered I/O for ffmpeg system
 * Copyright (c) 2000,2001 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "avformat.h"
#include "avio.h"
#include <stdarg.h>

#define IO_BUFFER_SIZE 32768

int init_put_byte(ByteIOContext *s,
                  unsigned char *buffer,
                  int buffer_size,
                  int write_flag,
                  void *opaque,
                  int (*read_packet)(void *opaque, uint8_t *buf, int buf_size),
                  void (*write_packet)(void *opaque, uint8_t *buf, int buf_size),
                  int (*seek)(void *opaque, offset_t offset, int whence))
{
    s->buffer = buffer;
    s->buffer_size = buffer_size;
    s->buf_ptr = buffer;
    s->write_flag = write_flag;
    if (!s->write_flag) 
        s->buf_end = buffer;
    else
        s->buf_end = buffer + buffer_size;
    s->opaque = opaque;
    s->write_packet = write_packet;
    s->read_packet = read_packet;
    s->seek = seek;
    s->pos = 0;
    s->must_flush = 0;
    s->eof_reached = 0;
    s->is_streamed = 0;
    s->max_packet_size = 0;
    s->update_checksum= NULL;
    return 0;
}
                  

#ifdef CONFIG_ENCODERS
static void flush_buffer(ByteIOContext *s)
{
    if (s->buf_ptr > s->buffer) {
        if (s->write_packet)
            s->write_packet(s->opaque, s->buffer, s->buf_ptr - s->buffer);
        if(s->update_checksum){
            s->checksum= s->update_checksum(s->checksum, s->checksum_ptr, s->buf_ptr - s->checksum_ptr);
            s->checksum_ptr= s->buffer;
        }
        s->pos += s->buf_ptr - s->buffer;
    }
    s->buf_ptr = s->buffer;
}

void put_byte(ByteIOContext *s, int b)
{
    *(s->buf_ptr)++ = b;
    if (s->buf_ptr >= s->buf_end) 
        flush_buffer(s);
}

void put_buffer(ByteIOContext *s, const unsigned char *buf, int size)
{
    int len;

    while (size > 0) {
        len = (s->buf_end - s->buf_ptr);
        if (len > size)
            len = size;
        memcpy(s->buf_ptr, buf, len);
        s->buf_ptr += len;

        if (s->buf_ptr >= s->buf_end) 
            flush_buffer(s);

        buf += len;
        size -= len;
    }
}

void put_flush_packet(ByteIOContext *s)
{
    flush_buffer(s);
    s->must_flush = 0;
}
#endif //CONFIG_ENCODERS

offset_t url_fseek(ByteIOContext *s, offset_t offset, int whence)
{
    offset_t offset1;

    if (whence != SEEK_CUR && whence != SEEK_SET)
        return -EINVAL;
    
#ifdef CONFIG_ENCODERS
    if (s->write_flag) {
        if (whence == SEEK_CUR) {
            offset1 = s->pos + (s->buf_ptr - s->buffer);
            if (offset == 0)
                return offset1;
            offset += offset1;
        }
        offset1 = offset - s->pos;
        if (!s->must_flush && 
            offset1 >= 0 && offset1 < (s->buf_end - s->buffer)) {
            /* can do the seek inside the buffer */
            s->buf_ptr = s->buffer + offset1;
        } else {
            if (!s->seek)
                return -EPIPE;
            flush_buffer(s);
            s->must_flush = 1;
            s->buf_ptr = s->buffer;
            s->seek(s->opaque, offset, SEEK_SET);
            s->pos = offset;
        }
    } else 
#endif //CONFIG_ENCODERS
    {
        if (whence == SEEK_CUR) {
            offset1 = s->pos - (s->buf_end - s->buffer) + (s->buf_ptr - s->buffer);
            if (offset == 0)
                return offset1;
            offset += offset1;
        }
        offset1 = offset - (s->pos - (s->buf_end - s->buffer));
        if (offset1 >= 0 && offset1 <= (s->buf_end - s->buffer)) {
            /* can do the seek inside the buffer */
            s->buf_ptr = s->buffer + offset1;
        } else {
            if (!s->seek)
                return -EPIPE;
            s->buf_ptr = s->buffer;
            s->buf_end = s->buffer;
            if (s->seek(s->opaque, offset, SEEK_SET) == (offset_t)-EPIPE)
                return -EPIPE;
            s->pos = offset;
        }
        s->eof_reached = 0;
    }
    return offset;
}

void url_fskip(ByteIOContext *s, offset_t offset)
{
    url_fseek(s, offset, SEEK_CUR);
}

offset_t url_ftell(ByteIOContext *s)
{
    return url_fseek(s, 0, SEEK_CUR);
}

int url_feof(ByteIOContext *s)
{
    return s->eof_reached;
}

#ifdef CONFIG_ENCODERS
void put_le32(ByteIOContext *s, unsigned int val)
{
    put_byte(s, val);
    put_byte(s, val >> 8);
    put_byte(s, val >> 16);
    put_byte(s, val >> 24);
}

void put_be32(ByteIOContext *s, unsigned int val)
{
    put_byte(s, val >> 24);
    put_byte(s, val >> 16);
    put_byte(s, val >> 8);
    put_byte(s, val);
}

/* IEEE format is assumed */
void put_be64_double(ByteIOContext *s, double val)
{
    union {
        double d;
        uint64_t ull;
    } u;
    u.d = val;
    put_be64(s, u.ull);
}

void put_strz(ByteIOContext *s, const char *str)
{
    if (str)
        put_buffer(s, (const unsigned char *) str, strlen(str) + 1);
    else
        put_byte(s, 0);
}

void put_le64(ByteIOContext *s, uint64_t val)
{
    put_le32(s, (uint32_t)(val & 0xffffffff));
    put_le32(s, (uint32_t)(val >> 32));
}

void put_be64(ByteIOContext *s, uint64_t val)
{
    put_be32(s, (uint32_t)(val >> 32));
    put_be32(s, (uint32_t)(val & 0xffffffff));
}

void put_le16(ByteIOContext *s, unsigned int val)
{
    put_byte(s, val);
    put_byte(s, val >> 8);
}

void put_be16(ByteIOContext *s, unsigned int val)
{
    put_byte(s, val >> 8);
    put_byte(s, val);
}

void put_tag(ByteIOContext *s, const char *tag)
{
    while (*tag) {
        put_byte(s, *tag++);
    }
}
#endif //CONFIG_ENCODERS

/* Input stream */

static void fill_buffer(ByteIOContext *s)
{
    int len;

    /* no need to do anything if EOF already reached */
    if (s->eof_reached)
        return;

    if(s->update_checksum){
        s->checksum= s->update_checksum(s->checksum, s->checksum_ptr, s->buf_end - s->checksum_ptr);
        s->checksum_ptr= s->buffer;
    }

    len = s->read_packet(s->opaque, s->buffer, s->buffer_size);
    if (len <= 0) {
        /* do not modify buffer if EOF reached so that a seek back can
           be done without rereading data */
        s->eof_reached = 1;
    } else {
        s->pos += len;
        s->buf_ptr = s->buffer;
        s->buf_end = s->buffer + len;
    }
}

unsigned long get_checksum(ByteIOContext *s){
    s->checksum= s->update_checksum(s->checksum, s->checksum_ptr, s->buf_ptr - s->checksum_ptr);
    s->update_checksum= NULL;
    return s->checksum;
}

void init_checksum(ByteIOContext *s, unsigned long (*update_checksum)(unsigned long c, const uint8_t *p, unsigned int len), unsigned long checksum){
    s->update_checksum= update_checksum;
    if(s->update_checksum){
        s->checksum= s->update_checksum(checksum, NULL, 0);
        s->checksum_ptr= s->buf_ptr;
    }
}

/* NOTE: return 0 if EOF, so you cannot use it if EOF handling is
   necessary */
/* XXX: put an inline version */
int get_byte(ByteIOContext *s)
{
    if (s->buf_ptr < s->buf_end) {
        return *s->buf_ptr++;
    } else {
        fill_buffer(s);
        if (s->buf_ptr < s->buf_end)
            return *s->buf_ptr++;
        else
            return 0;
    }
}

/* NOTE: return URL_EOF (-1) if EOF */
int url_fgetc(ByteIOContext *s)
{
    if (s->buf_ptr < s->buf_end) {
        return *s->buf_ptr++;
    } else {
        fill_buffer(s);
        if (s->buf_ptr < s->buf_end)
            return *s->buf_ptr++;
        else
            return URL_EOF;
    }
}

int get_buffer(ByteIOContext *s, unsigned char *buf, int size)
{
    int len, size1;

    size1 = size;
    while (size > 0) {
        len = s->buf_end - s->buf_ptr;
        if (len > size)
            len = size;
        if (len == 0) {
            fill_buffer(s);
            len = s->buf_end - s->buf_ptr;
            if (len == 0)
                break;
        } else {
            memcpy(buf, s->buf_ptr, len);
            buf += len;
            s->buf_ptr += len;
            size -= len;
        }
    }
    return size1 - size;
}

int get_partial_buffer(ByteIOContext *s, unsigned char *buf, int size)
{
    int len;

    len = s->buf_end - s->buf_ptr;
    if (len == 0) {
        fill_buffer(s);
        len = s->buf_end - s->buf_ptr;
    }
    if (len > size)
        len = size;
    memcpy(buf, s->buf_ptr, len);
    s->buf_ptr += len;
    return len;
}

unsigned int get_le16(ByteIOContext *s)
{
    unsigned int val;
    val = get_byte(s);
    val |= get_byte(s) << 8;
    return val;
}

unsigned int get_le32(ByteIOContext *s)
{
    unsigned int val;
    val = get_byte(s);
    val |= get_byte(s) << 8;
    val |= get_byte(s) << 16;
    val |= get_byte(s) << 24;
    return val;
}

uint64_t get_le64(ByteIOContext *s)
{
    uint64_t val;
    val = (uint64_t)get_le32(s);
    val |= (uint64_t)get_le32(s) << 32;
    return val;
}

unsigned int get_be16(ByteIOContext *s)
{
    unsigned int val;
    val = get_byte(s) << 8;
    val |= get_byte(s);
    return val;
}

unsigned int get_be32(ByteIOContext *s)
{
    unsigned int val;
    val = get_byte(s) << 24;
    val |= get_byte(s) << 16;
    val |= get_byte(s) << 8;
    val |= get_byte(s);
    return val;
}

double get_be64_double(ByteIOContext *s)
{
    union {
        double d;
        uint64_t ull;
    } u;

    u.ull = get_be64(s);
    return u.d;
}

char *get_strz(ByteIOContext *s, char *buf, int maxlen)
{
    int i = 0;
    char c;

    while ((c = get_byte(s))) {
        if (i < maxlen-1)
            buf[i++] = c;
    }
    
    buf[i] = 0; /* Ensure null terminated, but may be truncated */

    return buf;
}

uint64_t get_be64(ByteIOContext *s)
{
    uint64_t val;
    val = (uint64_t)get_be32(s) << 32;
    val |= (uint64_t)get_be32(s);
    return val;
}

/* link with avio functions */

#ifdef CONFIG_ENCODERS
static void url_write_packet(void *opaque, uint8_t *buf, int buf_size)
{
    URLContext *h = opaque;
    url_write(h, buf, buf_size);
}
#else
#define	url_write_packet NULL
#endif //CONFIG_ENCODERS

static int url_read_packet(void *opaque, uint8_t *buf, int buf_size)
{
    URLContext *h = opaque;
    return url_read(h, buf, buf_size);
}

static int url_seek_packet(void *opaque, int64_t offset, int whence)
{
    URLContext *h = opaque;
    return url_seek(h, offset, whence);
    //return 0;
}

int url_fdopen(ByteIOContext *s, URLContext *h)
{
    uint8_t *buffer;
    int buffer_size, max_packet_size;

    
    max_packet_size = url_get_max_packet_size(h);
    if (max_packet_size) {
        buffer_size = max_packet_size; /* no need to bufferize more than one packet */
    } else {
        buffer_size = IO_BUFFER_SIZE;
    }
    buffer = av_malloc(buffer_size);
    if (!buffer)
        return -ENOMEM;

    if (init_put_byte(s, buffer, buffer_size, 
                      (h->flags & URL_WRONLY || h->flags & URL_RDWR), h,
                      url_read_packet, url_write_packet, url_seek_packet) < 0) {
        av_free(buffer);
        return AVERROR_IO;
    }
    s->is_streamed = h->is_streamed;
    s->max_packet_size = max_packet_size;
    return 0;
}

/* XXX: must be called before any I/O */
int url_setbufsize(ByteIOContext *s, int buf_size)
{
    uint8_t *buffer;
    buffer = av_malloc(buf_size);
    if (!buffer)
        return -ENOMEM;

    av_free(s->buffer);
    s->buffer = buffer;
    s->buffer_size = buf_size;
    s->buf_ptr = buffer;
    if (!s->write_flag) 
        s->buf_end = buffer;
    else
        s->buf_end = buffer + buf_size;
    return 0;
}

/* NOTE: when opened as read/write, the buffers are only used for
   reading */
int url_fopen(ByteIOContext *s, const char *filename, int flags)
{
    URLContext *h;
    int err;

    err = url_open(&h, filename, flags);
    if (err < 0)
        return err;
    err = url_fdopen(s, h);
    if (err < 0) {
        url_close(h);
        return err;
    }
    return 0;
}

int url_fclose(ByteIOContext *s)
{
    URLContext *h = s->opaque;
    
    av_free(s->buffer);
    memset(s, 0, sizeof(ByteIOContext));
    return url_close(h);
}

URLContext *url_fileno(ByteIOContext *s)
{
    return s->opaque;
}

#ifdef CONFIG_ENCODERS
/* XXX: currently size is limited */
int url_fprintf(ByteIOContext *s, const char *fmt, ...)
{
    va_list ap;
    char buf[4096];
    int ret;

    va_start(ap, fmt);
    ret = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    put_buffer(s, buf, strlen(buf));
    return ret;
}
#endif //CONFIG_ENCODERS

/* note: unlike fgets, the EOL character is not returned and a whole
   line is parsed. return NULL if first char read was EOF */
char *url_fgets(ByteIOContext *s, char *buf, int buf_size)
{
    int c;
    char *q;

    c = url_fgetc(s);
    if (c == EOF)
        return NULL;
    q = buf;
    for(;;) {
        if (c == EOF || c == '\n')
            break;
        if ((q - buf) < buf_size - 1)
            *q++ = c;
        c = url_fgetc(s);
    }
    if (buf_size > 0)
        *q = '\0';
    return buf;
}

/* 
 * Return the maximum packet size associated to packetized buffered file
 * handle. If the file is not packetized (stream like http or file on
 * disk), then 0 is returned.
 * 
 * @param h buffered file handle
 * @return maximum packet size in bytes
 */
int url_fget_max_packet_size(ByteIOContext *s)
{
    return s->max_packet_size;
}

#ifdef CONFIG_ENCODERS
/* buffer handling */
int url_open_buf(ByteIOContext *s, uint8_t *buf, int buf_size, int flags)
{
    return init_put_byte(s, buf, buf_size, 
                         (flags & URL_WRONLY || flags & URL_RDWR),
                         NULL, NULL, NULL, NULL);
}

/* return the written or read size */
int url_close_buf(ByteIOContext *s)
{
    put_flush_packet(s);
    return s->buf_ptr - s->buffer;
}

/* output in a dynamic buffer */

typedef struct DynBuffer {
    int pos, size, allocated_size;
    uint8_t *buffer;
    int io_buffer_size;
    uint8_t io_buffer[1];
} DynBuffer;

static void dyn_buf_write(void *opaque, uint8_t *buf, int buf_size)
{
    DynBuffer *d = opaque;
    int new_size, new_allocated_size;
    
    /* reallocate buffer if needed */
    new_size = d->pos + buf_size;
    new_allocated_size = d->allocated_size;
    while (new_size > new_allocated_size) {
        if (!new_allocated_size)
            new_allocated_size = new_size;
        else
            new_allocated_size = (new_allocated_size * 3) / 2 + 1;    
    }
    
    if (new_allocated_size > d->allocated_size) {
        d->buffer = av_realloc(d->buffer, new_allocated_size);
        if(d->buffer == NULL)
             return ;
        d->allocated_size = new_allocated_size;
    }
    memcpy(d->buffer + d->pos, buf, buf_size);
    d->pos = new_size;
    if (d->pos > d->size)
        d->size = d->pos;
}

static void dyn_packet_buf_write(void *opaque, uint8_t *buf, int buf_size)
{
    unsigned char buf1[4];

    /* packetized write: output the header */
    buf1[0] = (buf_size >> 24);
    buf1[1] = (buf_size >> 16);
    buf1[2] = (buf_size >> 8);
    buf1[3] = (buf_size);
    dyn_buf_write(opaque, buf1, 4);

    /* then the data */
    dyn_buf_write(opaque, buf, buf_size);
}

static int dyn_buf_seek(void *opaque, offset_t offset, int whence)
{
    DynBuffer *d = opaque;

    if (whence == SEEK_CUR)
        offset += d->pos;
    else if (whence == SEEK_END)
        offset += d->size;
    if (offset < 0 || offset > 0x7fffffffLL)
        return -1;
    d->pos = offset;
    return 0;
}

static int url_open_dyn_buf_internal(ByteIOContext *s, int max_packet_size)
{
    DynBuffer *d;
    int io_buffer_size, ret;
    
    if (max_packet_size) 
        io_buffer_size = max_packet_size;
    else
        io_buffer_size = 1024;
        
    d = av_malloc(sizeof(DynBuffer) + io_buffer_size);
    if (!d)
        return -1;
    d->io_buffer_size = io_buffer_size;
    d->buffer = NULL;
    d->pos = 0;
    d->size = 0;
    d->allocated_size = 0;
    ret = init_put_byte(s, d->io_buffer, io_buffer_size, 
                        1, d, NULL, 
                        max_packet_size ? dyn_packet_buf_write : dyn_buf_write, 
                        max_packet_size ? NULL : dyn_buf_seek);
    if (ret == 0) {
        s->max_packet_size = max_packet_size;
    }
    return ret;
}

/*
 * Open a write only memory stream.
 * 
 * @param s new IO context
 * @return zero if no error.
 */
int url_open_dyn_buf(ByteIOContext *s)
{
    return url_open_dyn_buf_internal(s, 0);
}

/*
 * Open a write only packetized memory stream with a maximum packet
 * size of 'max_packet_size'.  The stream is stored in a memory buffer
 * with a big endian 4 byte header giving the packet size in bytes.
 * 
 * @param s new IO context
 * @param max_packet_size maximum packet size (must be > 0) 
 * @return zero if no error.
 */
int url_open_dyn_packet_buf(ByteIOContext *s, int max_packet_size)
{
    if (max_packet_size <= 0)
        return -1;
    return url_open_dyn_buf_internal(s, max_packet_size);
}

/* 
 * Return the written size and a pointer to the buffer. The buffer
 *  must be freed with av_free(). 
 * @param s IO context
 * @param pointer to a byte buffer
 * @return the length of the byte buffer
 */
int url_close_dyn_buf(ByteIOContext *s, uint8_t **pbuffer)
{
    DynBuffer *d = s->opaque;
    int size;

    put_flush_packet(s);

    *pbuffer = d->buffer;
    size = d->size;
    av_free(d);
    return size;
}
#endif //CONFIG_ENCODERS
