/*
 * buffered I/O
 * Copyright (c) 2000,2001 Fabrice Bellard
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

#include "libavutil/bprint.h"
#include "libavutil/crc.h"
#include "libavutil/dict.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "libavutil/avassert.h"
#include "avformat.h"
#include "avio.h"
#include "avio_internal.h"
#include "internal.h"
#include "url.h"
#include <stdarg.h>

#define IO_BUFFER_SIZE 32768

/**
 * Do seeks within this distance ahead of the current buffer by skipping
 * data instead of calling the protocol seek function, for seekable
 * protocols.
 */
#define SHORT_SEEK_THRESHOLD 4096

typedef struct AVIOInternal {
    URLContext *h;
} AVIOInternal;

static void *ff_avio_child_next(void *obj, void *prev)
{
    AVIOContext *s = obj;
    AVIOInternal *internal = s->opaque;
    return prev ? NULL : internal->h;
}

static const AVClass *ff_avio_child_class_next(const AVClass *prev)
{
    return prev ? NULL : &ffurl_context_class;
}

#define OFFSET(x) offsetof(AVIOContext,x)
#define E AV_OPT_FLAG_ENCODING_PARAM
#define D AV_OPT_FLAG_DECODING_PARAM
static const AVOption ff_avio_options[] = {
    {"protocol_whitelist", "List of protocols that are allowed to be used", OFFSET(protocol_whitelist), AV_OPT_TYPE_STRING, { .str = NULL },  CHAR_MIN, CHAR_MAX, D },
    { NULL },
};

const AVClass ff_avio_class = {
    .class_name = "AVIOContext",
    .item_name  = av_default_item_name,
    .version    = LIBAVUTIL_VERSION_INT,
    .option     = ff_avio_options,
    .child_next = ff_avio_child_next,
    .child_class_next = ff_avio_child_class_next,
};

static void fill_buffer(AVIOContext *s);
static int url_resetbuf(AVIOContext *s, int flags);

int ffio_init_context(AVIOContext *s,
                  unsigned char *buffer,
                  int buffer_size,
                  int write_flag,
                  void *opaque,
                  int (*read_packet)(void *opaque, uint8_t *buf, int buf_size),
                  int (*write_packet)(void *opaque, uint8_t *buf, int buf_size),
                  int64_t (*seek)(void *opaque, int64_t offset, int whence))
{
    memset(s, 0, sizeof(AVIOContext));

    s->buffer      = buffer;
    s->orig_buffer_size =
    s->buffer_size = buffer_size;
    s->buf_ptr     = buffer;
    s->buf_ptr_max = buffer;
    s->opaque      = opaque;
    s->direct      = 0;

    url_resetbuf(s, write_flag ? AVIO_FLAG_WRITE : AVIO_FLAG_READ);

    s->write_packet    = write_packet;
    s->read_packet     = read_packet;
    s->seek            = seek;
    s->pos             = 0;
    s->eof_reached     = 0;
    s->error           = 0;
    s->seekable        = seek ? AVIO_SEEKABLE_NORMAL : 0;
    s->min_packet_size = 0;
    s->max_packet_size = 0;
    s->update_checksum = NULL;
    s->short_seek_threshold = SHORT_SEEK_THRESHOLD;

    if (!read_packet && !write_flag) {
        s->pos     = buffer_size;
        s->buf_end = s->buffer + buffer_size;
    }
    s->read_pause = NULL;
    s->read_seek  = NULL;

    s->write_data_type       = NULL;
    s->ignore_boundary_point = 0;
    s->current_type          = AVIO_DATA_MARKER_UNKNOWN;
    s->last_time             = AV_NOPTS_VALUE;
    s->short_seek_get        = NULL;
    s->written               = 0;

    return 0;
}

AVIOContext *avio_alloc_context(
                  unsigned char *buffer,
                  int buffer_size,
                  int write_flag,
                  void *opaque,
                  int (*read_packet)(void *opaque, uint8_t *buf, int buf_size),
                  int (*write_packet)(void *opaque, uint8_t *buf, int buf_size),
                  int64_t (*seek)(void *opaque, int64_t offset, int whence))
{
    AVIOContext *s = av_malloc(sizeof(AVIOContext));
    if (!s)
        return NULL;
    ffio_init_context(s, buffer, buffer_size, write_flag, opaque,
                  read_packet, write_packet, seek);
    return s;
}

void avio_context_free(AVIOContext **ps)
{
    av_freep(ps);
}

static void writeout(AVIOContext *s, const uint8_t *data, int len)
{
    if (!s->error) {
        int ret = 0;
        if (s->write_data_type)
            ret = s->write_data_type(s->opaque, (uint8_t *)data,
                                     len,
                                     s->current_type,
                                     s->last_time);
        else if (s->write_packet)
            ret = s->write_packet(s->opaque, (uint8_t *)data, len);
        if (ret < 0) {
            s->error = ret;
        } else {
            if (s->pos + len > s->written)
                s->written = s->pos + len;
        }
    }
    if (s->current_type == AVIO_DATA_MARKER_SYNC_POINT ||
        s->current_type == AVIO_DATA_MARKER_BOUNDARY_POINT) {
        s->current_type = AVIO_DATA_MARKER_UNKNOWN;
    }
    s->last_time = AV_NOPTS_VALUE;
    s->writeout_count ++;
    s->pos += len;
}

static void flush_buffer(AVIOContext *s)
{
    s->buf_ptr_max = FFMAX(s->buf_ptr, s->buf_ptr_max);
    if (s->write_flag && s->buf_ptr_max > s->buffer) {
        writeout(s, s->buffer, s->buf_ptr_max - s->buffer);
        if (s->update_checksum) {
            s->checksum     = s->update_checksum(s->checksum, s->checksum_ptr,
                                                 s->buf_ptr_max - s->checksum_ptr);
            s->checksum_ptr = s->buffer;
        }
    }
    s->buf_ptr = s->buf_ptr_max = s->buffer;
    if (!s->write_flag)
        s->buf_end = s->buffer;
}

void avio_w8(AVIOContext *s, int b)
{
    av_assert2(b>=-128 && b<=255);
    *s->buf_ptr++ = b;
    if (s->buf_ptr >= s->buf_end)
        flush_buffer(s);
}

void ffio_fill(AVIOContext *s, int b, int count)
{
    while (count > 0) {
        int len = FFMIN(s->buf_end - s->buf_ptr, count);
        memset(s->buf_ptr, b, len);
        s->buf_ptr += len;

        if (s->buf_ptr >= s->buf_end)
            flush_buffer(s);

        count -= len;
    }
}

void avio_write(AVIOContext *s, const unsigned char *buf, int size)
{
    if (s->direct && !s->update_checksum) {
        avio_flush(s);
        writeout(s, buf, size);
        return;
    }
    while (size > 0) {
        int len = FFMIN(s->buf_end - s->buf_ptr, size);
        memcpy(s->buf_ptr, buf, len);
        s->buf_ptr += len;

        if (s->buf_ptr >= s->buf_end)
            flush_buffer(s);

        buf += len;
        size -= len;
    }
}

void avio_flush(AVIOContext *s)
{
    int seekback = s->write_flag ? FFMIN(0, s->buf_ptr - s->buf_ptr_max) : 0;
    flush_buffer(s);
    if (seekback)
        avio_seek(s, seekback, SEEK_CUR);
}

int64_t avio_seek(AVIOContext *s, int64_t offset, int whence)
{
    int64_t offset1;
    int64_t pos;
    int force = whence & AVSEEK_FORCE;
    int buffer_size;
    int short_seek;
    whence &= ~AVSEEK_FORCE;

    if(!s)
        return AVERROR(EINVAL);

    buffer_size = s->buf_end - s->buffer;
    // pos is the absolute position that the beginning of s->buffer corresponds to in the file
    pos = s->pos - (s->write_flag ? 0 : buffer_size);

    if (whence != SEEK_CUR && whence != SEEK_SET)
        return AVERROR(EINVAL);

    if (whence == SEEK_CUR) {
        offset1 = pos + (s->buf_ptr - s->buffer);
        if (offset == 0)
            return offset1;
        if (offset > INT64_MAX - offset1)
            return AVERROR(EINVAL);
        offset += offset1;
    }
    if (offset < 0)
        return AVERROR(EINVAL);

    if (s->short_seek_get) {
        short_seek = s->short_seek_get(s->opaque);
        /* fallback to default short seek */
        if (short_seek <= 0)
            short_seek = s->short_seek_threshold;
    } else
        short_seek = s->short_seek_threshold;

    offset1 = offset - pos; // "offset1" is the relative offset from the beginning of s->buffer
    s->buf_ptr_max = FFMAX(s->buf_ptr_max, s->buf_ptr);
    if ((!s->direct || !s->seek) &&
        offset1 >= 0 && offset1 <= (s->write_flag ? s->buf_ptr_max - s->buffer : buffer_size)) {
        /* can do the seek inside the buffer */
        s->buf_ptr = s->buffer + offset1;
    } else if ((!(s->seekable & AVIO_SEEKABLE_NORMAL) ||
               offset1 <= buffer_size + short_seek) &&
               !s->write_flag && offset1 >= 0 &&
               (!s->direct || !s->seek) &&
              (whence != SEEK_END || force)) {
        while(s->pos < offset && !s->eof_reached)
            fill_buffer(s);
        if (s->eof_reached)
            return AVERROR_EOF;
        s->buf_ptr = s->buf_end - (s->pos - offset);
    } else if(!s->write_flag && offset1 < 0 && -offset1 < buffer_size>>1 && s->seek && offset > 0) {
        int64_t res;

        pos -= FFMIN(buffer_size>>1, pos);
        if ((res = s->seek(s->opaque, pos, SEEK_SET)) < 0)
            return res;
        s->buf_end =
        s->buf_ptr = s->buffer;
        s->pos = pos;
        s->eof_reached = 0;
        fill_buffer(s);
        return avio_seek(s, offset, SEEK_SET | force);
    } else {
        int64_t res;
        if (s->write_flag) {
            flush_buffer(s);
        }
        if (!s->seek)
            return AVERROR(EPIPE);
        if ((res = s->seek(s->opaque, offset, SEEK_SET)) < 0)
            return res;
        s->seek_count ++;
        if (!s->write_flag)
            s->buf_end = s->buffer;
        s->buf_ptr = s->buf_ptr_max = s->buffer;
        s->pos = offset;
    }
    s->eof_reached = 0;
    return offset;
}

int64_t avio_skip(AVIOContext *s, int64_t offset)
{
    return avio_seek(s, offset, SEEK_CUR);
}

int64_t avio_size(AVIOContext *s)
{
    int64_t size;

    if (!s)
        return AVERROR(EINVAL);

    if (s->written)
        return s->written;

    if (!s->seek)
        return AVERROR(ENOSYS);
    size = s->seek(s->opaque, 0, AVSEEK_SIZE);
    if (size < 0) {
        if ((size = s->seek(s->opaque, -1, SEEK_END)) < 0)
            return size;
        size++;
        s->seek(s->opaque, s->pos, SEEK_SET);
    }
    return size;
}

int avio_feof(AVIOContext *s)
{
    if(!s)
        return 0;
    if(s->eof_reached){
        s->eof_reached=0;
        fill_buffer(s);
    }
    return s->eof_reached;
}

void avio_wl32(AVIOContext *s, unsigned int val)
{
    avio_w8(s, (uint8_t) val       );
    avio_w8(s, (uint8_t)(val >> 8 ));
    avio_w8(s, (uint8_t)(val >> 16));
    avio_w8(s,           val >> 24 );
}

void avio_wb32(AVIOContext *s, unsigned int val)
{
    avio_w8(s,           val >> 24 );
    avio_w8(s, (uint8_t)(val >> 16));
    avio_w8(s, (uint8_t)(val >> 8 ));
    avio_w8(s, (uint8_t) val       );
}

int avio_put_str(AVIOContext *s, const char *str)
{
    int len = 1;
    if (str) {
        len += strlen(str);
        avio_write(s, (const unsigned char *) str, len);
    } else
        avio_w8(s, 0);
    return len;
}

static inline int put_str16(AVIOContext *s, const char *str, const int be)
{
    const uint8_t *q = str;
    int ret = 0;
    int err = 0;

    while (*q) {
        uint32_t ch;
        uint16_t tmp;

        GET_UTF8(ch, *q++, goto invalid;)
        PUT_UTF16(ch, tmp, be ? avio_wb16(s, tmp) : avio_wl16(s, tmp);
                  ret += 2;)
        continue;
invalid:
        av_log(s, AV_LOG_ERROR, "Invalid UTF8 sequence in avio_put_str16%s\n", be ? "be" : "le");
        err = AVERROR(EINVAL);
        if (!*(q-1))
            break;
    }
    if (be)
        avio_wb16(s, 0);
    else
        avio_wl16(s, 0);
    if (err)
        return err;
    ret += 2;
    return ret;
}

#define PUT_STR16(type, big_endian)                          \
int avio_put_str16 ## type(AVIOContext *s, const char *str)  \
{                                                            \
return put_str16(s, str, big_endian);                        \
}

PUT_STR16(le, 0)
PUT_STR16(be, 1)

#undef PUT_STR16

int ff_get_v_length(uint64_t val)
{
    int i = 1;

    while (val >>= 7)
        i++;

    return i;
}

void ff_put_v(AVIOContext *bc, uint64_t val)
{
    int i = ff_get_v_length(val);

    while (--i > 0)
        avio_w8(bc, 128 | (uint8_t)(val >> (7*i)));

    avio_w8(bc, val & 127);
}

void avio_wl64(AVIOContext *s, uint64_t val)
{
    avio_wl32(s, (uint32_t)(val & 0xffffffff));
    avio_wl32(s, (uint32_t)(val >> 32));
}

void avio_wb64(AVIOContext *s, uint64_t val)
{
    avio_wb32(s, (uint32_t)(val >> 32));
    avio_wb32(s, (uint32_t)(val & 0xffffffff));
}

void avio_wl16(AVIOContext *s, unsigned int val)
{
    avio_w8(s, (uint8_t)val);
    avio_w8(s, (int)val >> 8);
}

void avio_wb16(AVIOContext *s, unsigned int val)
{
    avio_w8(s, (int)val >> 8);
    avio_w8(s, (uint8_t)val);
}

void avio_wl24(AVIOContext *s, unsigned int val)
{
    avio_wl16(s, val & 0xffff);
    avio_w8(s, (int)val >> 16);
}

void avio_wb24(AVIOContext *s, unsigned int val)
{
    avio_wb16(s, (int)val >> 8);
    avio_w8(s, (uint8_t)val);
}

void avio_write_marker(AVIOContext *s, int64_t time, enum AVIODataMarkerType type)
{
    if (type == AVIO_DATA_MARKER_FLUSH_POINT) {
        if (s->buf_ptr - s->buffer >= s->min_packet_size)
            avio_flush(s);
        return;
    }
    if (!s->write_data_type)
        return;
    // If ignoring boundary points, just treat it as unknown
    if (type == AVIO_DATA_MARKER_BOUNDARY_POINT && s->ignore_boundary_point)
        type = AVIO_DATA_MARKER_UNKNOWN;
    // Avoid unnecessary flushes if we are already in non-header/trailer
    // data and setting the type to unknown
    if (type == AVIO_DATA_MARKER_UNKNOWN &&
        (s->current_type != AVIO_DATA_MARKER_HEADER &&
         s->current_type != AVIO_DATA_MARKER_TRAILER))
        return;

    switch (type) {
    case AVIO_DATA_MARKER_HEADER:
    case AVIO_DATA_MARKER_TRAILER:
        // For header/trailer, ignore a new marker of the same type;
        // consecutive header/trailer markers can be merged.
        if (type == s->current_type)
            return;
        break;
    }

    // If we've reached here, we have a new, noteworthy marker.
    // Flush the previous data and mark the start of the new data.
    avio_flush(s);
    s->current_type = type;
    s->last_time = time;
}

static int read_packet_wrapper(AVIOContext *s, uint8_t *buf, int size)
{
    int ret;

    if (!s->read_packet)
        return AVERROR(EINVAL);
    ret = s->read_packet(s->opaque, buf, size);
#if FF_API_OLD_AVIO_EOF_0
    if (!ret && !s->max_packet_size) {
        av_log(NULL, AV_LOG_WARNING, "Invalid return value 0 for stream protocol\n");
        ret = AVERROR_EOF;
    }
#else
    av_assert2(ret || s->max_packet_size);
#endif
    return ret;
}

/* Input stream */

static void fill_buffer(AVIOContext *s)
{
    int max_buffer_size = s->max_packet_size ?
                          s->max_packet_size : IO_BUFFER_SIZE;
    uint8_t *dst        = s->buf_end - s->buffer + max_buffer_size < s->buffer_size ?
                          s->buf_end : s->buffer;
    int len             = s->buffer_size - (dst - s->buffer);

    /* can't fill the buffer without read_packet, just set EOF if appropriate */
    if (!s->read_packet && s->buf_ptr >= s->buf_end)
        s->eof_reached = 1;

    /* no need to do anything if EOF already reached */
    if (s->eof_reached)
        return;

    if (s->update_checksum && dst == s->buffer) {
        if (s->buf_end > s->checksum_ptr)
            s->checksum = s->update_checksum(s->checksum, s->checksum_ptr,
                                             s->buf_end - s->checksum_ptr);
        s->checksum_ptr = s->buffer;
    }

    /* make buffer smaller in case it ended up large after probing */
    if (s->read_packet && s->orig_buffer_size && s->buffer_size > s->orig_buffer_size) {
        if (dst == s->buffer && s->buf_ptr != dst) {
            int ret = ffio_set_buf_size(s, s->orig_buffer_size);
            if (ret < 0)
                av_log(s, AV_LOG_WARNING, "Failed to decrease buffer size\n");

            s->checksum_ptr = dst = s->buffer;
        }
        av_assert0(len >= s->orig_buffer_size);
        len = s->orig_buffer_size;
    }

    len = read_packet_wrapper(s, dst, len);
    if (len == AVERROR_EOF) {
        /* do not modify buffer if EOF reached so that a seek back can
           be done without rereading data */
        s->eof_reached = 1;
    } else if (len < 0) {
        s->eof_reached = 1;
        s->error= len;
    } else {
        s->pos += len;
        s->buf_ptr = dst;
        s->buf_end = dst + len;
        s->bytes_read += len;
    }
}

unsigned long ff_crc04C11DB7_update(unsigned long checksum, const uint8_t *buf,
                                    unsigned int len)
{
    return av_crc(av_crc_get_table(AV_CRC_32_IEEE), checksum, buf, len);
}

unsigned long ff_crcEDB88320_update(unsigned long checksum, const uint8_t *buf,
                                    unsigned int len)
{
    return av_crc(av_crc_get_table(AV_CRC_32_IEEE_LE), checksum, buf, len);
}

unsigned long ff_crcA001_update(unsigned long checksum, const uint8_t *buf,
                                unsigned int len)
{
    return av_crc(av_crc_get_table(AV_CRC_16_ANSI_LE), checksum, buf, len);
}

unsigned long ffio_get_checksum(AVIOContext *s)
{
    s->checksum = s->update_checksum(s->checksum, s->checksum_ptr,
                                     s->buf_ptr - s->checksum_ptr);
    s->update_checksum = NULL;
    return s->checksum;
}

void ffio_init_checksum(AVIOContext *s,
                   unsigned long (*update_checksum)(unsigned long c, const uint8_t *p, unsigned int len),
                   unsigned long checksum)
{
    s->update_checksum = update_checksum;
    if (s->update_checksum) {
        s->checksum     = checksum;
        s->checksum_ptr = s->buf_ptr;
    }
}

/* XXX: put an inline version */
int avio_r8(AVIOContext *s)
{
    if (s->buf_ptr >= s->buf_end)
        fill_buffer(s);
    if (s->buf_ptr < s->buf_end)
        return *s->buf_ptr++;
    return 0;
}

int avio_read(AVIOContext *s, unsigned char *buf, int size)
{
    int len, size1;

    size1 = size;
    while (size > 0) {
        len = FFMIN(s->buf_end - s->buf_ptr, size);
        if (len == 0 || s->write_flag) {
            if((s->direct || size > s->buffer_size) && !s->update_checksum) {
                // bypass the buffer and read data directly into buf
                len = read_packet_wrapper(s, buf, size);
                if (len == AVERROR_EOF) {
                    /* do not modify buffer if EOF reached so that a seek back can
                    be done without rereading data */
                    s->eof_reached = 1;
                    break;
                } else if (len < 0) {
                    s->eof_reached = 1;
                    s->error= len;
                    break;
                } else {
                    s->pos += len;
                    s->bytes_read += len;
                    size -= len;
                    buf += len;
                    // reset the buffer
                    s->buf_ptr = s->buffer;
                    s->buf_end = s->buffer/* + len*/;
                }
            } else {
                fill_buffer(s);
                len = s->buf_end - s->buf_ptr;
                if (len == 0)
                    break;
            }
        } else {
            memcpy(buf, s->buf_ptr, len);
            buf += len;
            s->buf_ptr += len;
            size -= len;
        }
    }
    if (size1 == size) {
        if (s->error)      return s->error;
        if (avio_feof(s))  return AVERROR_EOF;
    }
    return size1 - size;
}

int ffio_read_size(AVIOContext *s, unsigned char *buf, int size)
{
    int ret = avio_read(s, buf, size);
    if (ret != size)
        return AVERROR_INVALIDDATA;
    return ret;
}

int ffio_read_indirect(AVIOContext *s, unsigned char *buf, int size, const unsigned char **data)
{
    if (s->buf_end - s->buf_ptr >= size && !s->write_flag) {
        *data = s->buf_ptr;
        s->buf_ptr += size;
        return size;
    } else {
        *data = buf;
        return avio_read(s, buf, size);
    }
}

int avio_read_partial(AVIOContext *s, unsigned char *buf, int size)
{
    int len;

    if (size < 0)
        return -1;

    if (s->read_packet && s->write_flag) {
        len = read_packet_wrapper(s, buf, size);
        if (len > 0)
            s->pos += len;
        return len;
    }

    len = s->buf_end - s->buf_ptr;
    if (len == 0) {
        /* Reset the buf_end pointer to the start of the buffer, to make sure
         * the fill_buffer call tries to read as much data as fits into the
         * full buffer, instead of just what space is left after buf_end.
         * This avoids returning partial packets at the end of the buffer,
         * for packet based inputs.
         */
        s->buf_end = s->buf_ptr = s->buffer;
        fill_buffer(s);
        len = s->buf_end - s->buf_ptr;
    }
    if (len > size)
        len = size;
    memcpy(buf, s->buf_ptr, len);
    s->buf_ptr += len;
    if (!len) {
        if (s->error)      return s->error;
        if (avio_feof(s))  return AVERROR_EOF;
    }
    return len;
}

unsigned int avio_rl16(AVIOContext *s)
{
    unsigned int val;
    val = avio_r8(s);
    val |= avio_r8(s) << 8;
    return val;
}

unsigned int avio_rl24(AVIOContext *s)
{
    unsigned int val;
    val = avio_rl16(s);
    val |= avio_r8(s) << 16;
    return val;
}

unsigned int avio_rl32(AVIOContext *s)
{
    unsigned int val;
    val = avio_rl16(s);
    val |= avio_rl16(s) << 16;
    return val;
}

uint64_t avio_rl64(AVIOContext *s)
{
    uint64_t val;
    val = (uint64_t)avio_rl32(s);
    val |= (uint64_t)avio_rl32(s) << 32;
    return val;
}

unsigned int avio_rb16(AVIOContext *s)
{
    unsigned int val;
    val = avio_r8(s) << 8;
    val |= avio_r8(s);
    return val;
}

unsigned int avio_rb24(AVIOContext *s)
{
    unsigned int val;
    val = avio_rb16(s) << 8;
    val |= avio_r8(s);
    return val;
}
unsigned int avio_rb32(AVIOContext *s)
{
    unsigned int val;
    val = avio_rb16(s) << 16;
    val |= avio_rb16(s);
    return val;
}

int ff_get_line(AVIOContext *s, char *buf, int maxlen)
{
    int i = 0;
    char c;

    do {
        c = avio_r8(s);
        if (c && i < maxlen-1)
            buf[i++] = c;
    } while (c != '\n' && c != '\r' && c);
    if (c == '\r' && avio_r8(s) != '\n' && !avio_feof(s))
        avio_skip(s, -1);

    buf[i] = 0;
    return i;
}

int ff_get_chomp_line(AVIOContext *s, char *buf, int maxlen)
{
    int len = ff_get_line(s, buf, maxlen);
    while (len > 0 && av_isspace(buf[len - 1]))
        buf[--len] = '\0';
    return len;
}

int64_t ff_read_line_to_bprint(AVIOContext *s, AVBPrint *bp)
{
    int len, end;
    int64_t read = 0;
    char tmp[1024];
    char c;

    do {
        len = 0;
        do {
            c = avio_r8(s);
            end = (c == '\r' || c == '\n' || c == '\0');
            if (!end)
                tmp[len++] = c;
        } while (!end && len < sizeof(tmp));
        av_bprint_append_data(bp, tmp, len);
        read += len;
    } while (!end);

    if (c == '\r' && avio_r8(s) != '\n' && !avio_feof(s))
        avio_skip(s, -1);

    if (!c && s->error)
        return s->error;

    if (!c && !read && avio_feof(s))
        return AVERROR_EOF;

    return read;
}

int64_t ff_read_line_to_bprint_overwrite(AVIOContext *s, AVBPrint *bp)
{
    int64_t ret;

    av_bprint_clear(bp);
    ret = ff_read_line_to_bprint(s, bp);
    if (ret < 0)
        return ret;

    if (!av_bprint_is_complete(bp))
        return AVERROR(ENOMEM);

    return bp->len;
}

int avio_get_str(AVIOContext *s, int maxlen, char *buf, int buflen)
{
    int i;

    if (buflen <= 0)
        return AVERROR(EINVAL);
    // reserve 1 byte for terminating 0
    buflen = FFMIN(buflen - 1, maxlen);
    for (i = 0; i < buflen; i++)
        if (!(buf[i] = avio_r8(s)))
            return i + 1;
    buf[i] = 0;
    for (; i < maxlen; i++)
        if (!avio_r8(s))
            return i + 1;
    return maxlen;
}

#define GET_STR16(type, read) \
    int avio_get_str16 ##type(AVIOContext *pb, int maxlen, char *buf, int buflen)\
{\
    char* q = buf;\
    int ret = 0;\
    if (buflen <= 0) \
        return AVERROR(EINVAL); \
    while (ret + 1 < maxlen) {\
        uint8_t tmp;\
        uint32_t ch;\
        GET_UTF16(ch, (ret += 2) <= maxlen ? read(pb) : 0, break;)\
        if (!ch)\
            break;\
        PUT_UTF8(ch, tmp, if (q - buf < buflen - 1) *q++ = tmp;)\
    }\
    *q = 0;\
    return ret;\
}\

GET_STR16(le, avio_rl16)
GET_STR16(be, avio_rb16)

#undef GET_STR16

uint64_t avio_rb64(AVIOContext *s)
{
    uint64_t val;
    val = (uint64_t)avio_rb32(s) << 32;
    val |= (uint64_t)avio_rb32(s);
    return val;
}

uint64_t ffio_read_varlen(AVIOContext *bc){
    uint64_t val = 0;
    int tmp;

    do{
        tmp = avio_r8(bc);
        val= (val<<7) + (tmp&127);
    }while(tmp&128);
    return val;
}

static int io_read_packet(void *opaque, uint8_t *buf, int buf_size)
{
    AVIOInternal *internal = opaque;
    return ffurl_read(internal->h, buf, buf_size);
}

static int io_write_packet(void *opaque, uint8_t *buf, int buf_size)
{
    AVIOInternal *internal = opaque;
    return ffurl_write(internal->h, buf, buf_size);
}

static int64_t io_seek(void *opaque, int64_t offset, int whence)
{
    AVIOInternal *internal = opaque;
    return ffurl_seek(internal->h, offset, whence);
}

static int io_short_seek(void *opaque)
{
    AVIOInternal *internal = opaque;
    return ffurl_get_short_seek(internal->h);
}

static int io_read_pause(void *opaque, int pause)
{
    AVIOInternal *internal = opaque;
    if (!internal->h->prot->url_read_pause)
        return AVERROR(ENOSYS);
    return internal->h->prot->url_read_pause(internal->h, pause);
}

static int64_t io_read_seek(void *opaque, int stream_index, int64_t timestamp, int flags)
{
    AVIOInternal *internal = opaque;
    if (!internal->h->prot->url_read_seek)
        return AVERROR(ENOSYS);
    return internal->h->prot->url_read_seek(internal->h, stream_index, timestamp, flags);
}

int ffio_fdopen(AVIOContext **s, URLContext *h)
{
    AVIOInternal *internal = NULL;
    uint8_t *buffer = NULL;
    int buffer_size, max_packet_size;

    max_packet_size = h->max_packet_size;
    if (max_packet_size) {
        buffer_size = max_packet_size; /* no need to bufferize more than one packet */
    } else {
        buffer_size = IO_BUFFER_SIZE;
    }
    buffer = av_malloc(buffer_size);
    if (!buffer)
        return AVERROR(ENOMEM);

    internal = av_mallocz(sizeof(*internal));
    if (!internal)
        goto fail;

    internal->h = h;

    *s = avio_alloc_context(buffer, buffer_size, h->flags & AVIO_FLAG_WRITE,
                            internal, io_read_packet, io_write_packet, io_seek);
    if (!*s)
        goto fail;

    (*s)->protocol_whitelist = av_strdup(h->protocol_whitelist);
    if (!(*s)->protocol_whitelist && h->protocol_whitelist) {
        avio_closep(s);
        goto fail;
    }
    (*s)->protocol_blacklist = av_strdup(h->protocol_blacklist);
    if (!(*s)->protocol_blacklist && h->protocol_blacklist) {
        avio_closep(s);
        goto fail;
    }
    (*s)->direct = h->flags & AVIO_FLAG_DIRECT;

    (*s)->seekable = h->is_streamed ? 0 : AVIO_SEEKABLE_NORMAL;
    (*s)->max_packet_size = max_packet_size;
    (*s)->min_packet_size = h->min_packet_size;
    if(h->prot) {
        (*s)->read_pause = io_read_pause;
        (*s)->read_seek  = io_read_seek;

        if (h->prot->url_read_seek)
            (*s)->seekable |= AVIO_SEEKABLE_TIME;
    }
    (*s)->short_seek_get = io_short_seek;
    (*s)->av_class = &ff_avio_class;
    return 0;
fail:
    av_freep(&internal);
    av_freep(&buffer);
    return AVERROR(ENOMEM);
}

URLContext* ffio_geturlcontext(AVIOContext *s)
{
    AVIOInternal *internal;
    if (!s)
        return NULL;

    internal = s->opaque;
    if (internal && s->read_packet == io_read_packet)
        return internal->h;
    else
        return NULL;
}

int ffio_ensure_seekback(AVIOContext *s, int64_t buf_size)
{
    uint8_t *buffer;
    int max_buffer_size = s->max_packet_size ?
                          s->max_packet_size : IO_BUFFER_SIZE;
    int filled = s->buf_end - s->buffer;
    ptrdiff_t checksum_ptr_offset = s->checksum_ptr ? s->checksum_ptr - s->buffer : -1;

    buf_size += s->buf_ptr - s->buffer + max_buffer_size;

    if (buf_size < filled || s->seekable || !s->read_packet)
        return 0;
    av_assert0(!s->write_flag);

    buffer = av_malloc(buf_size);
    if (!buffer)
        return AVERROR(ENOMEM);

    memcpy(buffer, s->buffer, filled);
    av_free(s->buffer);
    s->buf_ptr = buffer + (s->buf_ptr - s->buffer);
    s->buf_end = buffer + (s->buf_end - s->buffer);
    s->buffer = buffer;
    s->buffer_size = buf_size;
    if (checksum_ptr_offset >= 0)
        s->checksum_ptr = s->buffer + checksum_ptr_offset;
    return 0;
}

int ffio_set_buf_size(AVIOContext *s, int buf_size)
{
    uint8_t *buffer;
    buffer = av_malloc(buf_size);
    if (!buffer)
        return AVERROR(ENOMEM);

    av_free(s->buffer);
    s->buffer = buffer;
    s->orig_buffer_size =
    s->buffer_size = buf_size;
    s->buf_ptr = s->buf_ptr_max = buffer;
    url_resetbuf(s, s->write_flag ? AVIO_FLAG_WRITE : AVIO_FLAG_READ);
    return 0;
}

static int url_resetbuf(AVIOContext *s, int flags)
{
    av_assert1(flags == AVIO_FLAG_WRITE || flags == AVIO_FLAG_READ);

    if (flags & AVIO_FLAG_WRITE) {
        s->buf_end = s->buffer + s->buffer_size;
        s->write_flag = 1;
    } else {
        s->buf_end = s->buffer;
        s->write_flag = 0;
    }
    return 0;
}

int ffio_rewind_with_probe_data(AVIOContext *s, unsigned char **bufp, int buf_size)
{
    int64_t buffer_start;
    int buffer_size;
    int overlap, new_size, alloc_size;
    uint8_t *buf = *bufp;

    if (s->write_flag) {
        av_freep(bufp);
        return AVERROR(EINVAL);
    }

    buffer_size = s->buf_end - s->buffer;

    /* the buffers must touch or overlap */
    if ((buffer_start = s->pos - buffer_size) > buf_size) {
        av_freep(bufp);
        return AVERROR(EINVAL);
    }

    overlap = buf_size - buffer_start;
    new_size = buf_size + buffer_size - overlap;

    alloc_size = FFMAX(s->buffer_size, new_size);
    if (alloc_size > buf_size)
        if (!(buf = (*bufp) = av_realloc_f(buf, 1, alloc_size)))
            return AVERROR(ENOMEM);

    if (new_size > buf_size) {
        memcpy(buf + buf_size, s->buffer + overlap, buffer_size - overlap);
        buf_size = new_size;
    }

    av_free(s->buffer);
    s->buf_ptr = s->buffer = buf;
    s->buffer_size = alloc_size;
    s->pos = buf_size;
    s->buf_end = s->buf_ptr + buf_size;
    s->eof_reached = 0;

    return 0;
}

int avio_open(AVIOContext **s, const char *filename, int flags)
{
    return avio_open2(s, filename, flags, NULL, NULL);
}

int ffio_open_whitelist(AVIOContext **s, const char *filename, int flags,
                         const AVIOInterruptCB *int_cb, AVDictionary **options,
                         const char *whitelist, const char *blacklist
                        )
{
    URLContext *h;
    int err;

    err = ffurl_open_whitelist(&h, filename, flags, int_cb, options, whitelist, blacklist, NULL);
    if (err < 0)
        return err;
    err = ffio_fdopen(s, h);
    if (err < 0) {
        ffurl_close(h);
        return err;
    }
    return 0;
}

int avio_open2(AVIOContext **s, const char *filename, int flags,
               const AVIOInterruptCB *int_cb, AVDictionary **options)
{
    return ffio_open_whitelist(s, filename, flags, int_cb, options, NULL, NULL);
}

int ffio_open2_wrapper(struct AVFormatContext *s, AVIOContext **pb, const char *url, int flags,
                       const AVIOInterruptCB *int_cb, AVDictionary **options)
{
    return ffio_open_whitelist(pb, url, flags, int_cb, options, s->protocol_whitelist, s->protocol_blacklist);
}

int avio_close(AVIOContext *s)
{
    AVIOInternal *internal;
    URLContext *h;

    if (!s)
        return 0;

    avio_flush(s);
    internal = s->opaque;
    h        = internal->h;

    av_freep(&s->opaque);
    av_freep(&s->buffer);
    if (s->write_flag)
        av_log(s, AV_LOG_VERBOSE, "Statistics: %d seeks, %d writeouts\n", s->seek_count, s->writeout_count);
    else
        av_log(s, AV_LOG_VERBOSE, "Statistics: %"PRId64" bytes read, %d seeks\n", s->bytes_read, s->seek_count);
    av_opt_free(s);

    avio_context_free(&s);

    return ffurl_close(h);
}

int avio_closep(AVIOContext **s)
{
    int ret = avio_close(*s);
    *s = NULL;
    return ret;
}

int avio_printf(AVIOContext *s, const char *fmt, ...)
{
    va_list ap;
    char buf[4096]; /* update doc entry in avio.h if changed */
    int ret;

    va_start(ap, fmt);
    ret = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    avio_write(s, buf, strlen(buf));
    return ret;
}

int avio_pause(AVIOContext *s, int pause)
{
    if (!s->read_pause)
        return AVERROR(ENOSYS);
    return s->read_pause(s->opaque, pause);
}

int64_t avio_seek_time(AVIOContext *s, int stream_index,
                       int64_t timestamp, int flags)
{
    int64_t ret;
    if (!s->read_seek)
        return AVERROR(ENOSYS);
    ret = s->read_seek(s->opaque, stream_index, timestamp, flags);
    if (ret >= 0) {
        int64_t pos;
        s->buf_ptr = s->buf_end; // Flush buffer
        pos = s->seek(s->opaque, 0, SEEK_CUR);
        if (pos >= 0)
            s->pos = pos;
        else if (pos != AVERROR(ENOSYS))
            ret = pos;
    }
    return ret;
}

int avio_read_to_bprint(AVIOContext *h, AVBPrint *pb, size_t max_size)
{
    int ret;
    char buf[1024];
    while (max_size) {
        ret = avio_read(h, buf, FFMIN(max_size, sizeof(buf)));
        if (ret == AVERROR_EOF)
            return 0;
        if (ret <= 0)
            return ret;
        av_bprint_append_data(pb, buf, ret);
        if (!av_bprint_is_complete(pb))
            return AVERROR(ENOMEM);
        max_size -= ret;
    }
    return 0;
}

int avio_accept(AVIOContext *s, AVIOContext **c)
{
    int ret;
    AVIOInternal *internal = s->opaque;
    URLContext *sc = internal->h;
    URLContext *cc = NULL;
    ret = ffurl_accept(sc, &cc);
    if (ret < 0)
        return ret;
    return ffio_fdopen(c, cc);
}

int avio_handshake(AVIOContext *c)
{
    AVIOInternal *internal = c->opaque;
    URLContext *cc = internal->h;
    return ffurl_handshake(cc);
}

/* output in a dynamic buffer */

typedef struct DynBuffer {
    int pos, size, allocated_size;
    uint8_t *buffer;
    int io_buffer_size;
    uint8_t io_buffer[1];
} DynBuffer;

static int dyn_buf_write(void *opaque, uint8_t *buf, int buf_size)
{
    DynBuffer *d = opaque;
    unsigned new_size, new_allocated_size;

    /* reallocate buffer if needed */
    new_size = d->pos + buf_size;
    new_allocated_size = d->allocated_size;
    if (new_size < d->pos || new_size > INT_MAX/2)
        return -1;
    while (new_size > new_allocated_size) {
        if (!new_allocated_size)
            new_allocated_size = new_size;
        else
            new_allocated_size += new_allocated_size / 2 + 1;
    }

    if (new_allocated_size > d->allocated_size) {
        int err;
        if ((err = av_reallocp(&d->buffer, new_allocated_size)) < 0) {
            d->allocated_size = 0;
            d->size = 0;
            return err;
        }
        d->allocated_size = new_allocated_size;
    }
    memcpy(d->buffer + d->pos, buf, buf_size);
    d->pos = new_size;
    if (d->pos > d->size)
        d->size = d->pos;
    return buf_size;
}

static int dyn_packet_buf_write(void *opaque, uint8_t *buf, int buf_size)
{
    unsigned char buf1[4];
    int ret;

    /* packetized write: output the header */
    AV_WB32(buf1, buf_size);
    ret = dyn_buf_write(opaque, buf1, 4);
    if (ret < 0)
        return ret;

    /* then the data */
    return dyn_buf_write(opaque, buf, buf_size);
}

static int64_t dyn_buf_seek(void *opaque, int64_t offset, int whence)
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

static int url_open_dyn_buf_internal(AVIOContext **s, int max_packet_size)
{
    DynBuffer *d;
    unsigned io_buffer_size = max_packet_size ? max_packet_size : 1024;

    if (sizeof(DynBuffer) + io_buffer_size < io_buffer_size)
        return -1;
    d = av_mallocz(sizeof(DynBuffer) + io_buffer_size);
    if (!d)
        return AVERROR(ENOMEM);
    d->io_buffer_size = io_buffer_size;
    *s = avio_alloc_context(d->io_buffer, d->io_buffer_size, 1, d, NULL,
                            max_packet_size ? dyn_packet_buf_write : dyn_buf_write,
                            max_packet_size ? NULL : dyn_buf_seek);
    if(!*s) {
        av_free(d);
        return AVERROR(ENOMEM);
    }
    (*s)->max_packet_size = max_packet_size;
    return 0;
}

int avio_open_dyn_buf(AVIOContext **s)
{
    return url_open_dyn_buf_internal(s, 0);
}

int ffio_open_dyn_packet_buf(AVIOContext **s, int max_packet_size)
{
    if (max_packet_size <= 0)
        return -1;
    return url_open_dyn_buf_internal(s, max_packet_size);
}

int avio_get_dyn_buf(AVIOContext *s, uint8_t **pbuffer)
{
    DynBuffer *d;

    if (!s) {
        *pbuffer = NULL;
        return 0;
    }

    avio_flush(s);

    d = s->opaque;
    *pbuffer = d->buffer;

    return d->size;
}

int avio_close_dyn_buf(AVIOContext *s, uint8_t **pbuffer)
{
    DynBuffer *d;
    int size;
    static const char padbuf[AV_INPUT_BUFFER_PADDING_SIZE] = {0};
    int padding = 0;

    if (!s) {
        *pbuffer = NULL;
        return 0;
    }

    /* don't attempt to pad fixed-size packet buffers */
    if (!s->max_packet_size) {
        avio_write(s, padbuf, sizeof(padbuf));
        padding = AV_INPUT_BUFFER_PADDING_SIZE;
    }

    avio_flush(s);

    d = s->opaque;
    *pbuffer = d->buffer;
    size = d->size;
    av_free(d);

    avio_context_free(&s);

    return size - padding;
}

void ffio_free_dyn_buf(AVIOContext **s)
{
    uint8_t *tmp;
    if (!*s)
        return;
    avio_close_dyn_buf(*s, &tmp);
    av_free(tmp);
    *s = NULL;
}

static int null_buf_write(void *opaque, uint8_t *buf, int buf_size)
{
    DynBuffer *d = opaque;

    d->pos += buf_size;
    if (d->pos > d->size)
        d->size = d->pos;
    return buf_size;
}

int ffio_open_null_buf(AVIOContext **s)
{
    int ret = url_open_dyn_buf_internal(s, 0);
    if (ret >= 0) {
        AVIOContext *pb = *s;
        pb->write_packet = null_buf_write;
    }
    return ret;
}

int ffio_close_null_buf(AVIOContext *s)
{
    DynBuffer *d = s->opaque;
    int size;

    avio_flush(s);

    size = d->size;
    av_free(d);

    avio_context_free(&s);

    return size;
}
