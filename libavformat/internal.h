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

#define MAX_URL_SIZE 4096

#ifdef DEBUG
#    define hex_dump_debug(class, buf, size) av_hex_dump_log(class, AV_LOG_DEBUG, buf, size)
#else
#    define hex_dump_debug(class, buf, size)
#endif

typedef struct AVCodecTag {
    enum CodecID id;
    unsigned int tag;
} AVCodecTag;

#ifdef __GNUC__
#define dynarray_add(tab, nb_ptr, elem)\
do {\
    __typeof__(tab) _tab = (tab);\
    __typeof__(elem) _elem = (elem);\
    (void)sizeof(**_tab == _elem); /* check that types are compatible */\
    av_dynarray_add(_tab, nb_ptr, _elem);\
} while(0)
#else
#define dynarray_add(tab, nb_ptr, elem)\
do {\
    av_dynarray_add((tab), nb_ptr, (elem));\
} while(0)
#endif

struct tm *brktimegm(time_t secs, struct tm *tm);

char *ff_data_to_hex(char *buf, const uint8_t *src, int size, int lowercase);

/**
 * Parse a string of hexadecimal strings. Any space between the hexadecimal
 * digits is ignored.
 *
 * @param data if non-null, the parsed data is written to this pointer
 * @param p the string to parse
 * @return the number of bytes written (or to be written, if data is null)
 */
int ff_hex_to_data(uint8_t *data, const char *p);

void ff_program_add_stream_index(AVFormatContext *ac, int progid, unsigned int idx);

/**
 * Add packet to AVFormatContext->packet_buffer list, determining its
 * interleaved position using compare() function argument.
 * @return 0, or < 0 on error
 */
int ff_interleave_add_packet(AVFormatContext *s, AVPacket *pkt,
                              int (*compare)(AVFormatContext *, AVPacket *, AVPacket *));

void ff_read_frame_flush(AVFormatContext *s);

#define NTP_OFFSET 2208988800ULL
#define NTP_OFFSET_US (NTP_OFFSET * 1000000ULL)

/** Get the current time since NTP epoch in microseconds. */
uint64_t ff_ntp_time(void);

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
 * Append the media-specific SDP fragment for the media stream c
 * to the buffer buff.
 *
 * Note, the buffer needs to be initialized, since it is appended to
 * existing content.
 *
 * @param buff the buffer to append the SDP fragment to
 * @param size the size of the buff buffer
 * @param c the AVCodecContext of the media to describe
 * @param dest_addr the destination address of the media stream, may be NULL
 * @param dest_type the destination address type, may be NULL
 * @param port the destination port of the media stream, 0 if unknown
 * @param ttl the time to live of the stream, 0 if not multicast
 * @param fmt the AVFormatContext, which might contain options modifying
 *            the generated SDP
 */
void ff_sdp_write_media(char *buff, int size, AVCodecContext *c,
                        const char *dest_addr, const char *dest_type,
                        int port, int ttl, AVFormatContext *fmt);

/**
 * Write a packet to another muxer than the one the user originally
 * intended. Useful when chaining muxers, where one muxer internally
 * writes a received packet to another muxer.
 *
 * @param dst the muxer to write the packet to
 * @param dst_stream the stream index within dst to write the packet to
 * @param pkt the packet to be written
 * @param src the muxer the packet originally was intended for
 * @return the value av_write_frame returned
 */
int ff_write_chained(AVFormatContext *dst, int dst_stream, AVPacket *pkt,
                     AVFormatContext *src);

/**
 * Get the length in bytes which is needed to store val as v.
 */
int ff_get_v_length(uint64_t val);

/**
 * Put val using a variable number of bytes.
 */
void ff_put_v(AVIOContext *bc, uint64_t val);

/**
 * Read a whole line of text from AVIOContext. Stop reading after reaching
 * either a \\n, a \\0 or EOF. The returned string is always \\0-terminated,
 * and may be truncated if the buffer is too small.
 *
 * @param s the read-only AVIOContext
 * @param buf buffer to store the read line
 * @param maxlen size of the buffer
 * @return the length of the string written in the buffer, not including the
 *         final \\0
 */
int ff_get_line(AVIOContext *s, char *buf, int maxlen);

#define SPACE_CHARS " \t\r\n"

/**
 * Callback function type for ff_parse_key_value.
 *
 * @param key a pointer to the key
 * @param key_len the number of bytes that belong to the key, including the '='
 *                char
 * @param dest return the destination pointer for the value in *dest, may
 *             be null to ignore the value
 * @param dest_len the length of the *dest buffer
 */
typedef void (*ff_parse_key_val_cb)(void *context, const char *key,
                                    int key_len, char **dest, int *dest_len);
/**
 * Parse a string with comma-separated key=value pairs. The value strings
 * may be quoted and may contain escaped characters within quoted strings.
 *
 * @param str the string to parse
 * @param callback_get_buf function that returns where to store the
 *                         unescaped value string.
 * @param context the opaque context pointer to pass to callback_get_buf
 */
void ff_parse_key_value(const char *str, ff_parse_key_val_cb callback_get_buf,
                        void *context);

/**
 * Find stream index based on format-specific stream ID
 * @return stream index, or < 0 on error
 */
int ff_find_stream_index(AVFormatContext *s, int id);

/**
 * Internal version of av_index_search_timestamp
 */
int ff_index_search_timestamp(const AVIndexEntry *entries, int nb_entries,
                              int64_t wanted_timestamp, int flags);

/**
 * Internal version of av_add_index_entry
 */
int ff_add_index_entry(AVIndexEntry **index_entries,
                       int *nb_index_entries,
                       unsigned int *index_entries_allocated_size,
                       int64_t pos, int64_t timestamp, int size, int distance, int flags);

/**
 * Add a new chapter.
 *
 * @param s media file handle
 * @param id unique ID for this chapter
 * @param start chapter start time in time_base units
 * @param end chapter end time in time_base units
 * @param title chapter title
 *
 * @return AVChapter or NULL on error
 */
AVChapter *avpriv_new_chapter(AVFormatContext *s, int id, AVRational time_base,
                              int64_t start, int64_t end, const char *title);

/**
 * Ensure the index uses less memory than the maximum specified in
 * AVFormatContext.max_index_size by discarding entries if it grows
 * too large.
 */
void ff_reduce_index(AVFormatContext *s, int stream_index);

/*
 * Convert a relative url into an absolute url, given a base url.
 *
 * @param buf the buffer where output absolute url is written
 * @param size the size of buf
 * @param base the base url, may be equal to buf.
 * @param rel the new url, which is interpreted relative to base
 */
void ff_make_absolute_url(char *buf, int size, const char *base,
                          const char *rel);

enum CodecID ff_guess_image2_codec(const char *filename);

/**
 * Convert a date string in ISO8601 format to Unix timestamp.
 */
int64_t ff_iso8601_to_unix_time(const char *datestr);

/**
 * Perform a binary search using av_index_search_timestamp() and
 * AVInputFormat.read_timestamp().
 *
 * @param target_ts target timestamp in the time base of the given stream
 * @param stream_index stream number
 */
int ff_seek_frame_binary(AVFormatContext *s, int stream_index,
                         int64_t target_ts, int flags);

/**
 * Update cur_dts of all streams based on the given timestamp and AVStream.
 *
 * Stream ref_st unchanged, others set cur_dts in their native time base.
 * Only needed for timestamp wrapping or if (dts not set and pts!=dts).
 * @param timestamp new dts expressed in time_base of param ref_st
 * @param ref_st reference stream giving time_base of param timestamp
 */
void ff_update_cur_dts(AVFormatContext *s, AVStream *ref_st, int64_t timestamp);

/**
 * Perform a binary search using read_timestamp().
 *
 * @param target_ts target timestamp in the time base of the given stream
 * @param stream_index stream number
 */
int64_t ff_gen_search(AVFormatContext *s, int stream_index,
                      int64_t target_ts, int64_t pos_min,
                      int64_t pos_max, int64_t pos_limit,
                      int64_t ts_min, int64_t ts_max,
                      int flags, int64_t *ts_ret,
                      int64_t (*read_timestamp)(struct AVFormatContext *, int , int64_t *, int64_t ));

#endif /* AVFORMAT_INTERNAL_H */
