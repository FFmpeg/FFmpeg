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

/**
 * @file
 * unbuffered private I/O API
 */

#ifndef AVFORMAT_URL_H
#define AVFORMAT_URL_H

#include "avio.h"
#include "libavformat/version.h"

#include "libavutil/dict.h"
#include "libavutil/log.h"

#define URL_PROTOCOL_FLAG_NESTED_SCHEME 1 /*< The protocol name can be the first part of a nested protocol scheme */
#define URL_PROTOCOL_FLAG_NETWORK       2 /*< The protocol uses network */

extern int (*url_interrupt_cb)(void);

extern const AVClass ffurl_context_class;

typedef struct URLContext {
    const AVClass *av_class;    /**< information for av_log(). Set by url_open(). */
    struct URLProtocol *prot;
    void *priv_data;
    char *filename;             /**< specified URL */
    int flags;
    int max_packet_size;        /**< if non zero, the stream is packetized with this max packet size */
    int is_streamed;            /**< true if streamed (no seek possible), default = false */
    int is_connected;
    AVIOInterruptCB interrupt_callback;
    int64_t rw_timeout;         /**< maximum time to wait for (network) read/write operation completion, in mcs */
} URLContext;

typedef struct URLProtocol {
    const char *name;
    int     (*url_open)( URLContext *h, const char *url, int flags);
    /**
     * This callback is to be used by protocols which open further nested
     * protocols. options are then to be passed to ffurl_open()/ffurl_connect()
     * for those nested protocols.
     */
    int     (*url_open2)(URLContext *h, const char *url, int flags, AVDictionary **options);

    /**
     * Read data from the protocol.
     * If data is immediately available (even less than size), EOF is
     * reached or an error occurs (including EINTR), return immediately.
     * Otherwise:
     * In non-blocking mode, return AVERROR(EAGAIN) immediately.
     * In blocking mode, wait for data/EOF/error with a short timeout (0.1s),
     * and return AVERROR(EAGAIN) on timeout.
     * Checking interrupt_callback, looping on EINTR and EAGAIN and until
     * enough data has been read is left to the calling function; see
     * retry_transfer_wrapper in avio.c.
     */
    int     (*url_read)( URLContext *h, unsigned char *buf, int size);
    int     (*url_write)(URLContext *h, const unsigned char *buf, int size);
    int64_t (*url_seek)( URLContext *h, int64_t pos, int whence);
    int     (*url_close)(URLContext *h);
    struct URLProtocol *next;
    int (*url_read_pause)(URLContext *h, int pause);
    int64_t (*url_read_seek)(URLContext *h, int stream_index,
                             int64_t timestamp, int flags);
    int (*url_get_file_handle)(URLContext *h);
    int (*url_get_multi_file_handle)(URLContext *h, int **handles,
                                     int *numhandles);
    int (*url_shutdown)(URLContext *h, int flags);
    int priv_data_size;
    const AVClass *priv_data_class;
    int flags;
    int (*url_check)(URLContext *h, int mask);
} URLProtocol;

/**
 * Create a URLContext for accessing to the resource indicated by
 * url, but do not initiate the connection yet.
 *
 * @param puc pointer to the location where, in case of success, the
 * function puts the pointer to the created URLContext
 * @param flags flags which control how the resource indicated by url
 * is to be opened
 * @param int_cb interrupt callback to use for the URLContext, may be
 * NULL
 * @return >= 0 in case of success, a negative value corresponding to an
 * AVERROR code in case of failure
 */
int ffurl_alloc(URLContext **puc, const char *filename, int flags,
                const AVIOInterruptCB *int_cb);

/**
 * Connect an URLContext that has been allocated by ffurl_alloc
 *
 * @param options  A dictionary filled with options for nested protocols,
 * i.e. it will be passed to url_open2() for protocols implementing it.
 * This parameter will be destroyed and replaced with a dict containing options
 * that were not found. May be NULL.
 */
int ffurl_connect(URLContext *uc, AVDictionary **options);

/**
 * Create an URLContext for accessing to the resource indicated by
 * url, and open it.
 *
 * @param puc pointer to the location where, in case of success, the
 * function puts the pointer to the created URLContext
 * @param flags flags which control how the resource indicated by url
 * is to be opened
 * @param int_cb interrupt callback to use for the URLContext, may be
 * NULL
 * @param options  A dictionary filled with protocol-private options. On return
 * this parameter will be destroyed and replaced with a dict containing options
 * that were not found. May be NULL.
 * @return >= 0 in case of success, a negative value corresponding to an
 * AVERROR code in case of failure
 */
int ffurl_open(URLContext **puc, const char *filename, int flags,
               const AVIOInterruptCB *int_cb, AVDictionary **options);

/**
 * Read up to size bytes from the resource accessed by h, and store
 * the read bytes in buf.
 *
 * @return The number of bytes actually read, or a negative value
 * corresponding to an AVERROR code in case of error. A value of zero
 * indicates that it is not possible to read more from the accessed
 * resource (except if the value of the size argument is also zero).
 */
int ffurl_read(URLContext *h, unsigned char *buf, int size);

/**
 * Read as many bytes as possible (up to size), calling the
 * read function multiple times if necessary.
 * This makes special short-read handling in applications
 * unnecessary, if the return value is < size then it is
 * certain there was either an error or the end of file was reached.
 */
int ffurl_read_complete(URLContext *h, unsigned char *buf, int size);

/**
 * Write size bytes from buf to the resource accessed by h.
 *
 * @return the number of bytes actually written, or a negative value
 * corresponding to an AVERROR code in case of failure
 */
int ffurl_write(URLContext *h, const unsigned char *buf, int size);

/**
 * Change the position that will be used by the next read/write
 * operation on the resource accessed by h.
 *
 * @param pos specifies the new position to set
 * @param whence specifies how pos should be interpreted, it must be
 * one of SEEK_SET (seek from the beginning), SEEK_CUR (seek from the
 * current position), SEEK_END (seek from the end), or AVSEEK_SIZE
 * (return the filesize of the requested resource, pos is ignored).
 * @return a negative value corresponding to an AVERROR code in case
 * of failure, or the resulting file position, measured in bytes from
 * the beginning of the file. You can use this feature together with
 * SEEK_CUR to read the current file position.
 */
int64_t ffurl_seek(URLContext *h, int64_t pos, int whence);

/**
 * Close the resource accessed by the URLContext h, and free the
 * memory used by it. Also set the URLContext pointer to NULL.
 *
 * @return a negative value if an error condition occurred, 0
 * otherwise
 */
int ffurl_closep(URLContext **h);
int ffurl_close(URLContext *h);

/**
 * Return the filesize of the resource accessed by h, AVERROR(ENOSYS)
 * if the operation is not supported by h, or another negative value
 * corresponding to an AVERROR error code in case of failure.
 */
int64_t ffurl_size(URLContext *h);

/**
 * Return the file descriptor associated with this URL. For RTP, this
 * will return only the RTP file descriptor, not the RTCP file descriptor.
 *
 * @return the file descriptor associated with this URL, or <0 on error.
 */
int ffurl_get_file_handle(URLContext *h);

/**
 * Return the file descriptors associated with this URL.
 *
 * @return 0 on success or <0 on error.
 */
int ffurl_get_multi_file_handle(URLContext *h, int **handles, int *numhandles);

/**
 * Signal the URLContext that we are done reading or writing the stream.
 *
 * @param h pointer to the resource
 * @param flags flags which control how the resource indicated by url
 * is to be shutdown
 *
 * @return a negative value if an error condition occurred, 0
 * otherwise
 */
int ffurl_shutdown(URLContext *h, int flags);

/**
 * Register the URLProtocol protocol.
 */
int ffurl_register_protocol(URLProtocol *protocol);

/**
 * Check if the user has requested to interrup a blocking function
 * associated with cb.
 */
int ff_check_interrupt(AVIOInterruptCB *cb);

/**
 * Iterate over all available protocols.
 *
 * @param prev result of the previous call to this functions or NULL.
 */
URLProtocol *ffurl_protocol_next(const URLProtocol *prev);

/* udp.c */
int ff_udp_set_remote_url(URLContext *h, const char *uri);
int ff_udp_get_local_port(URLContext *h);

/**
 * Assemble a URL string from components. This is the reverse operation
 * of av_url_split.
 *
 * Note, this requires networking to be initialized, so the caller must
 * ensure ff_network_init has been called.
 *
 * @see av_url_split
 *
 * @param str the buffer to fill with the url
 * @param size the size of the str buffer
 * @param proto the protocol identifier, if null, the separator
 *              after the identifier is left out, too
 * @param authorization an optional authorization string, may be null.
 *                      An empty string is treated the same as a null string.
 * @param hostname the host name string
 * @param port the port number, left out from the string if negative
 * @param fmt a generic format string for everything to add after the
 *            host/port, may be null
 * @return the number of characters written to the destination buffer
 */
int ff_url_join(char *str, int size, const char *proto,
                const char *authorization, const char *hostname,
                int port, const char *fmt, ...) av_printf_format(7, 8);

/**
 * Convert a relative url into an absolute url, given a base url.
 *
 * @param buf the buffer where output absolute url is written
 * @param size the size of buf
 * @param base the base url, may be equal to buf.
 * @param rel the new url, which is interpreted relative to base
 */
void ff_make_absolute_url(char *buf, int size, const char *base,
                          const char *rel);


#endif /* AVFORMAT_URL_H */
