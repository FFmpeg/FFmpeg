/*
 * Buffered I/O for ffmpeg system
 * Copyright (c) 2000,2001 Gerard Lantau
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <stdlib.h>
#include <stdio.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <sys/time.h>
#include <getopt.h>
#include <string.h>

#include "avformat.h"

#define IO_BUFFER_SIZE 32768

int init_put_byte(ByteIOContext *s,
                  unsigned char *buffer,
                  int buffer_size,
                  int write_flag,
                  void *opaque,
                  int (*read_packet)(void *opaque, UINT8 *buf, int buf_size),
                  void (*write_packet)(void *opaque, UINT8 *buf, int buf_size),
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
    s->packet_size = 1;
    return 0;
}
                  

static void flush_buffer(ByteIOContext *s)
{
    if (s->buf_ptr > s->buffer) {
        if (s->write_packet)
            s->write_packet(s->opaque, s->buffer, s->buf_ptr - s->buffer);
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

void put_buffer(ByteIOContext *s, unsigned char *buf, int size)
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

offset_t url_fseek(ByteIOContext *s, offset_t offset, int whence)
{
    offset_t offset1;

    if (whence != SEEK_CUR && whence != SEEK_SET)
        return -EINVAL;
    
    if (s->write_flag) {
        if (whence == SEEK_CUR) {
            offset1 = s->pos + s->buf_ptr - s->buffer;
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
    } else {
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
            s->eof_reached = 0;
            s->seek(s->opaque, offset, SEEK_SET);
            s->pos = offset;
        }
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

void put_le64(ByteIOContext *s, unsigned long long val)
{
    put_le32(s, val & 0xffffffff);
    put_le32(s, val >> 32);
}

void put_be64(ByteIOContext *s, unsigned long long val)
{
    put_be32(s, val >> 32);
    put_be32(s, val & 0xffffffff);
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

void put_tag(ByteIOContext *s, char *tag)
{
    while (*tag) {
        put_byte(s, *tag++);
    }
}

/* Input stream */

static void fill_buffer(ByteIOContext *s)
{
    int len;

    len = s->read_packet(s->opaque, s->buffer, s->buffer_size);
    s->pos += len;
    s->buf_ptr = s->buffer;
    s->buf_end = s->buffer + len;
    if (len == 0) {
        s->eof_reached = 1;
    }
}

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

unsigned long long get_le64(ByteIOContext *s)
{
    UINT64 val;
    val = (UINT64)get_le32(s);
    val |= (UINT64)get_le32(s) << 32;
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

unsigned long long get_be64(ByteIOContext *s)
{
    UINT64 val;
    val = (UINT64)get_be32(s) << 32;
    val |= (UINT64)get_be32(s);
    return val;
}

/* link with avio functions */

void url_write_packet(void *opaque, UINT8 *buf, int buf_size)
{
    URLContext *h = opaque;
    url_write(h, buf, buf_size);
}

int url_read_packet(void *opaque, UINT8 *buf, int buf_size)
{
    URLContext *h = opaque;
    return url_read(h, buf, buf_size);
}

int url_seek_packet(void *opaque, long long offset, int whence)
{
    URLContext *h = opaque;
    url_seek(h, offset, whence);
    return 0;
}

int url_fdopen(ByteIOContext *s, URLContext *h)
{
    UINT8 *buffer;
    int buffer_size;

    buffer_size = (IO_BUFFER_SIZE / h->packet_size) * h->packet_size;
    buffer = malloc(buffer_size);
    if (!buffer)
        return -ENOMEM;

    if (init_put_byte(s, buffer, buffer_size, 
                      (h->flags & URL_WRONLY) != 0, h,
                      url_read_packet, url_write_packet, url_seek_packet) < 0) {
        free(buffer);
        return -EIO;
    }
    s->is_streamed = h->is_streamed;
    s->packet_size = h->packet_size;
    return 0;
}

/* XXX: must be called before any I/O */
int url_setbufsize(ByteIOContext *s, int buf_size)
{
    UINT8 *buffer;
    buffer = malloc(buf_size);
    if (!buffer)
        return -ENOMEM;

    free(s->buffer);
    s->buffer = buffer;
    s->buffer_size = buf_size;
    s->buf_ptr = buffer;
    if (!s->write_flag) 
        s->buf_end = buffer;
    else
        s->buf_end = buffer + buf_size;
    return 0;
}

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
    
    free(s->buffer);
    memset(s, 0, sizeof(ByteIOContext));
    return url_close(h);
}

URLContext *url_fileno(ByteIOContext *s)
{
    return s->opaque;
}

/* buffer handling */
int url_open_buf(ByteIOContext *s, UINT8 *buf, int buf_size, int flags)
{
    return init_put_byte(s, buf, buf_size, 
                         (flags & URL_WRONLY) != 0, NULL, NULL, NULL, NULL);
}

/* return the written or read size */
int url_close_buf(ByteIOContext *s)
{
    return s->buf_ptr - s->buffer;
}
