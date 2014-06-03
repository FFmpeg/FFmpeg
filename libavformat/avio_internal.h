/*
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

#ifndef AVFORMAT_AVIO_INTERNAL_H
#define AVFORMAT_AVIO_INTERNAL_H

#include "avio.h"
#include "url.h"

#include "libavutil/log.h"

extern const AVClass ffio_url_class;

int ffio_init_context(AVIOContext *s,
                  unsigned char *buffer,
                  int buffer_size,
                  int write_flag,
                  void *opaque,
                  int (*read_packet)(void *opaque, uint8_t *buf, int buf_size),
                  int (*write_packet)(void *opaque, uint8_t *buf, int buf_size),
                  int64_t (*seek)(void *opaque, int64_t offset, int whence));


/**
 * Read size bytes from AVIOContext, returning a pointer.
 * Note that the data pointed at by the returned pointer is only
 * valid until the next call that references the same IO context.
 * @param s IO context
 * @param buf pointer to buffer into which to assemble the requested
 *    data if it is not available in contiguous addresses in the
 *    underlying buffer
 * @param size number of bytes requested
 * @param data address at which to store pointer: this will be a
 *    a direct pointer into the underlying buffer if the requested
 *    number of bytes are available at contiguous addresses, otherwise
 *    will be a copy of buf
 * @return number of bytes read or AVERROR
 */
int ffio_read_indirect(AVIOContext *s, unsigned char *buf, int size, const unsigned char **data);

/**
 * Read size bytes from AVIOContext into buf.
 * This reads at most 1 packet. If that is not enough fewer bytes will be
 * returned.
 * @return number of bytes read or AVERROR
 */
int ffio_read_partial(AVIOContext *s, unsigned char *buf, int size);

void ffio_fill(AVIOContext *s, int b, int count);

static av_always_inline void ffio_wfourcc(AVIOContext *pb, const uint8_t *s)
{
    avio_wl32(pb, MKTAG(s[0], s[1], s[2], s[3]));
}

/**
 * Rewind the AVIOContext using the specified buffer containing the first buf_size bytes of the file.
 * Used after probing to avoid seeking.
 * Joins buf and s->buffer, taking any overlap into consideration.
 * @note s->buffer must overlap with buf or they can't be joined and the function fails
 *
 * @param s The read-only AVIOContext to rewind
 * @param buf The probe buffer containing the first buf_size bytes of the file
 * @param buf_size The size of buf
 * @return >= 0 in case of success, a negative value corresponding to an
 * AVERROR code in case of failure
 */
int ffio_rewind_with_probe_data(AVIOContext *s, unsigned char **buf, int buf_size);

uint64_t ffio_read_varlen(AVIOContext *bc);

/** @warning must be called before any I/O */
int ffio_set_buf_size(AVIOContext *s, int buf_size);

/**
 * Ensures that the requested seekback buffer size will be available
 *
 * Will ensure that when reading sequentially up to buf_size, seeking
 * within the current pos and pos+buf_size is possible.
 * Once the stream position moves outside this window this guarantee is lost.
 */
int ffio_ensure_seekback(AVIOContext *s, int buf_size);

int ffio_limit(AVIOContext *s, int size);

void ffio_init_checksum(AVIOContext *s,
                        unsigned long (*update_checksum)(unsigned long c, const uint8_t *p, unsigned int len),
                        unsigned long checksum);
unsigned long ffio_get_checksum(AVIOContext *s);
unsigned long ff_crc04C11DB7_update(unsigned long checksum, const uint8_t *buf,
                                    unsigned int len);
unsigned long ff_crcA001_update(unsigned long checksum, const uint8_t *buf,
                                unsigned int len);

/**
 * Open a write only packetized memory stream with a maximum packet
 * size of 'max_packet_size'.  The stream is stored in a memory buffer
 * with a big-endian 4 byte header giving the packet size in bytes.
 *
 * @param s new IO context
 * @param max_packet_size maximum packet size (must be > 0)
 * @return zero if no error.
 */
int ffio_open_dyn_packet_buf(AVIOContext **s, int max_packet_size);

/**
 * Create and initialize a AVIOContext for accessing the
 * resource referenced by the URLContext h.
 * @note When the URLContext h has been opened in read+write mode, the
 * AVIOContext can be used only for writing.
 *
 * @param s Used to return the pointer to the created AVIOContext.
 * In case of failure the pointed to value is set to NULL.
 * @return >= 0 in case of success, a negative value corresponding to an
 * AVERROR code in case of failure
 */
int ffio_fdopen(AVIOContext **s, URLContext *h);

/**
 * Open a write-only fake memory stream. The written data is not stored
 * anywhere - this is only used for measuring the amount of data
 * written.
 *
 * @param s new IO context
 * @return zero if no error.
 */
int ffio_open_null_buf(AVIOContext **s);

/**
 * Close a null buffer.
 *
 * @param s an IO context opened by ffio_open_null_buf
 * @return the number of bytes written to the null buffer
 */
int ffio_close_null_buf(AVIOContext *s);

#endif /* AVFORMAT_AVIO_INTERNAL_H */
