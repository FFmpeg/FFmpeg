/*
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

#ifndef AVFORMAT_INTERNAL_H
#define AVFORMAT_INTERNAL_H

#include <stdint.h>
#include "avformat.h"

void ff_dynarray_add(intptr_t **tab_ptr, int *nb_ptr, intptr_t elem);

#ifdef __GNUC__
#define dynarray_add(tab, nb_ptr, elem)\
do {\
    __typeof__(tab) _tab = (tab);\
    __typeof__(elem) _elem = (elem);\
    (void)sizeof(**_tab == _elem); /* check that types are compatible */\
    ff_dynarray_add((intptr_t **)_tab, nb_ptr, (intptr_t)_elem);\
} while(0)
#else
#define dynarray_add(tab, nb_ptr, elem)\
do {\
    ff_dynarray_add((intptr_t **)(tab), nb_ptr, (intptr_t)(elem));\
} while(0)
#endif

time_t mktimegm(struct tm *tm);
struct tm *brktimegm(time_t secs, struct tm *tm);
const char *small_strptime(const char *p, const char *fmt,
                           struct tm *dt);

char *ff_data_to_hex(char *buf, const uint8_t *src, int size, int lowercase);

void ff_program_add_stream_index(AVFormatContext *ac, int progid, unsigned int idx);

/**
 * Add packet to AVFormatContext->packet_buffer list, determining its
 * interleaved position using compare() function argument.
 */
void ff_interleave_add_packet(AVFormatContext *s, AVPacket *pkt,
                              int (*compare)(AVFormatContext *, AVPacket *, AVPacket *));

void ff_read_frame_flush(AVFormatContext *s);

#define NTP_OFFSET 2208988800ULL
#define NTP_OFFSET_US (NTP_OFFSET * 1000000ULL)

/** Gets the current time since NTP epoch in microseconds. */
uint64_t ff_ntp_time(void);

/**
 * Probes a bytestream to determine the input format. Each time a probe returns
 * with a score that is too low, the probe buffer size is increased and another
 * attempt is made. When the maximum probe size is reached, the input format
 * with the highest score is returned.
 *
 * @param pb the bytestream to probe, it may be closed and opened again
 * @param fmt the input format is put here
 * @param filename the filename of the stream
 * @param logctx the log context
 * @param offset the offset within the bytestream to probe from
 * @param max_probe_size the maximum probe buffer size (zero for default)
 * @return 0 in case of success, a negative value corresponding to an
 * AVERROR code otherwise
 */
int ff_probe_input_buffer(ByteIOContext **pb, AVInputFormat **fmt,
                          const char *filename, void *logctx,
                          unsigned int offset, unsigned int max_probe_size);

/**
 * Splits a URL string into components. To reassemble components back into
 * a URL, use ff_url_join instead of using snprintf directly.
 *
 * The pointers to buffers for storing individual components may be null,
 * in order to ignore that component. Buffers for components not found are
 * set to empty strings. If the port isn't found, it is set to a negative
 * value.
 *
 * @see ff_url_join
 *
 * @param proto the buffer for the protocol
 * @param proto_size the size of the proto buffer
 * @param authorization the buffer for the authorization
 * @param authorization_size the size of the authorization buffer
 * @param hostname the buffer for the host name
 * @param hostname_size the size of the hostname buffer
 * @param port_ptr a pointer to store the port number in
 * @param path the buffer for the path
 * @param path_size the size of the path buffer
 * @param url the URL to split
 */
void ff_url_split(char *proto, int proto_size,
                  char *authorization, int authorization_size,
                  char *hostname, int hostname_size,
                  int *port_ptr,
                  char *path, int path_size,
                  const char *url);

/**
 * Assembles a URL string from components. This is the reverse operation
 * of ff_url_split.
 *
 * Note, this requires networking to be initialized, so the caller must
 * ensure ff_network_init has been called.
 *
 * @see ff_url_split
 *
 * @param str the buffer to fill with the url
 * @param size the size of the str buffer
 * @param proto the protocol identifier, if null, the separator
 *              after the identifier is left out, too
 * @param authorization an optional authorization string, may be null
 * @param hostname the host name string
 * @param port the port number, left out from the string if negative
 * @param fmt a generic format string for everything to add after the
 *            host/port, may be null
 * @return the number of characters written to the destination buffer
 */
int ff_url_join(char *str, int size, const char *proto,
                const char *authorization, const char *hostname,
                int port, const char *fmt, ...);

/**
 * Appends the media-specific SDP fragment for the media stream c
 * to the buffer buff.
 *
 * Note, the buffer needs to be initialized, since it is appended to
 * existing content.
 *
 * @param buff the buffer to append the SDP fragment to
 * @param size the size of the buff buffer
 * @param c the AVCodecContext of the media to describe
 * @param dest_addr the destination address of the media stream, may be NULL
 * @param port the destination port of the media stream, 0 if unknown
 * @param ttl the time to live of the stream, 0 if not multicast
 */
void ff_sdp_write_media(char *buff, int size, AVCodecContext *c,
                        const char *dest_addr, int port, int ttl);

#endif /* AVFORMAT_INTERNAL_H */
