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
#include "os_support.h"

#define MAX_URL_SIZE 4096

/** size of probe buffer, for guessing file type from file contents */
#define PROBE_BUF_MIN 2048
#define PROBE_BUF_MAX (1 << 20)

#define MAX_PROBE_PACKETS 2500

#ifdef DEBUG
#    define hex_dump_debug(class, buf, size) av_hex_dump_log(class, AV_LOG_DEBUG, buf, size)
#else
#    define hex_dump_debug(class, buf, size) do { if (0) av_hex_dump_log(class, AV_LOG_DEBUG, buf, size); } while(0)
#endif

typedef struct AVCodecTag {
    enum AVCodecID id;
    unsigned int tag;
} AVCodecTag;

typedef struct CodecMime{
    char str[32];
    enum AVCodecID id;
} CodecMime;

/*************************************************/
/* fractional numbers for exact pts handling */

/**
 * The exact value of the fractional number is: 'val + num / den'.
 * num is assumed to be 0 <= num < den.
 */
typedef struct FFFrac {
    int64_t val, num, den;
} FFFrac;


struct AVFormatInternal {
    /**
     * Number of streams relevant for interleaving.
     * Muxing only.
     */
    int nb_interleaved_streams;

    /**
     * This buffer is only needed when packets were already buffered but
     * not decoded, for example to get the codec parameters in MPEG
     * streams.
     */
    struct AVPacketList *packet_buffer;
    struct AVPacketList *packet_buffer_end;

    /* av_seek_frame() support */
    int64_t data_offset; /**< offset of the first packet */

    /**
     * Raw packets from the demuxer, prior to parsing and decoding.
     * This buffer is used for buffering packets until the codec can
     * be identified, as parsing cannot be done without knowing the
     * codec.
     */
    struct AVPacketList *raw_packet_buffer;
    struct AVPacketList *raw_packet_buffer_end;
    /**
     * Packets split by the parser get queued here.
     */
    struct AVPacketList *parse_queue;
    struct AVPacketList *parse_queue_end;
    /**
     * Remaining size available for raw_packet_buffer, in bytes.
     */
#define RAW_PACKET_BUFFER_SIZE 2500000
    int raw_packet_buffer_remaining_size;

    /**
     * Offset to remap timestamps to be non-negative.
     * Expressed in timebase units.
     * @see AVStream.mux_ts_offset
     */
    int64_t offset;

    /**
     * Timebase for the timestamp offset.
     */
    AVRational offset_timebase;

#if FF_API_COMPUTE_PKT_FIELDS2
    int missing_ts_warning;
#endif

    int inject_global_side_data;

    int avoid_negative_ts_use_pts;
};

struct AVStreamInternal {
    /**
     * Set to 1 if the codec allows reordering, so pts can be different
     * from dts.
     */
    int reorder;
};

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

struct tm *ff_brktimegm(time_t secs, struct tm *tm);

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
 * Append the media-specific SDP fragment for the media stream c
 * to the buffer buff.
 *
 * Note, the buffer needs to be initialized, since it is appended to
 * existing content.
 *
 * @param buff the buffer to append the SDP fragment to
 * @param size the size of the buff buffer
 * @param st the AVStream of the media to describe
 * @param idx the global stream index
 * @param dest_addr the destination address of the media stream, may be NULL
 * @param dest_type the destination address type, may be NULL
 * @param port the destination port of the media stream, 0 if unknown
 * @param ttl the time to live of the stream, 0 if not multicast
 * @param fmt the AVFormatContext, which might contain options modifying
 *            the generated SDP
 */
void ff_sdp_write_media(char *buff, int size, AVStream *st, int idx,
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
 * @param interleave 0->use av_write_frame, 1->av_interleaved_write_frame
 * @return the value av_write_frame returned
 */
int ff_write_chained(AVFormatContext *dst, int dst_stream, AVPacket *pkt,
                     AVFormatContext *src, int interleave);

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

void ff_configure_buffers_for_index(AVFormatContext *s, int64_t time_tolerance);

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

enum AVCodecID ff_guess_image2_codec(const char *filename);

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

int ff_find_last_ts(AVFormatContext *s, int stream_index, int64_t *ts, int64_t *pos,
                    int64_t (*read_timestamp)(struct AVFormatContext *, int , int64_t *, int64_t ));

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

/**
 * Set the time base and wrapping info for a given stream. This will be used
 * to interpret the stream's timestamps. If the new time base is invalid
 * (numerator or denominator are non-positive), it leaves the stream
 * unchanged.
 *
 * @param s stream
 * @param pts_wrap_bits number of bits effectively used by the pts
 *        (used for wrap control)
 * @param pts_num time base numerator
 * @param pts_den time base denominator
 */
void avpriv_set_pts_info(AVStream *s, int pts_wrap_bits,
                         unsigned int pts_num, unsigned int pts_den);

/**
 * Add side data to a packet for changing parameters to the given values.
 * Parameters set to 0 aren't included in the change.
 */
int ff_add_param_change(AVPacket *pkt, int32_t channels,
                        uint64_t channel_layout, int32_t sample_rate,
                        int32_t width, int32_t height);

/**
 * Set the timebase for each stream from the corresponding codec timebase and
 * print it.
 */
int ff_framehash_write_header(AVFormatContext *s);

/**
 * Read a transport packet from a media file.
 *
 * @param s media file handle
 * @param pkt is filled
 * @return 0 if OK, AVERROR_xxx on error
 */
int ff_read_packet(AVFormatContext *s, AVPacket *pkt);

/**
 * Interleave a packet per dts in an output media file.
 *
 * Packets with pkt->destruct == av_destruct_packet will be freed inside this
 * function, so they cannot be used after it. Note that calling av_packet_unref()
 * on them is still safe.
 *
 * @param s media file handle
 * @param out the interleaved packet will be output here
 * @param pkt the input packet
 * @param flush 1 if no further packets are available as input and all
 *              remaining packets should be output
 * @return 1 if a packet was output, 0 if no packet could be output,
 *         < 0 if an error occurred
 */
int ff_interleave_packet_per_dts(AVFormatContext *s, AVPacket *out,
                                 AVPacket *pkt, int flush);

void ff_free_stream(AVFormatContext *s, AVStream *st);

/**
 * Return the frame duration in seconds. Return 0 if not available.
 */
void ff_compute_frame_duration(AVFormatContext *s, int *pnum, int *pden, AVStream *st,
                               AVCodecParserContext *pc, AVPacket *pkt);

unsigned int ff_codec_get_tag(const AVCodecTag *tags, enum AVCodecID id);

enum AVCodecID ff_codec_get_id(const AVCodecTag *tags, unsigned int tag);

/**
 * Select a PCM codec based on the given parameters.
 *
 * @param bps     bits-per-sample
 * @param flt     floating-point
 * @param be      big-endian
 * @param sflags  signed flags. each bit corresponds to one byte of bit depth.
 *                e.g. the 1st bit indicates if 8-bit should be signed or
 *                unsigned, the 2nd bit indicates if 16-bit should be signed or
 *                unsigned, etc... This is useful for formats such as WAVE where
 *                only 8-bit is unsigned and all other bit depths are signed.
 * @return        a PCM codec id or AV_CODEC_ID_NONE
 */
enum AVCodecID ff_get_pcm_codec_id(int bps, int flt, int be, int sflags);

/**
 * Chooses a timebase for muxing the specified stream.
 *
 * The chosen timebase allows sample accurate timestamps based
 * on the framerate or sample rate for audio streams. It also is
 * at least as precise as 1/min_precision would be.
 */
AVRational ff_choose_timebase(AVFormatContext *s, AVStream *st, int min_precision);

/**
 * Chooses a timebase for muxing the specified stream.
 */
enum AVChromaLocation ff_choose_chroma_location(AVFormatContext *s, AVStream *st);

/**
 * Generate standard extradata for AVC-Intra based on width/height and field
 * order.
 */
int ff_generate_avci_extradata(AVStream *st);

/**
 * Wrap errno on rename() error.
 *
 * @param oldpath source path
 * @param newpath destination path
 * @return        0 or AVERROR on failure
 */
static inline int ff_rename(const char *oldpath, const char *newpath, void *logctx)
{
    int ret = 0;
    if (rename(oldpath, newpath) == -1) {
        ret = AVERROR(errno);
        if (logctx)
            av_log(logctx, AV_LOG_ERROR, "failed to rename file %s to %s\n", oldpath, newpath);
    }
    return ret;
}

/**
 * Allocate extradata with additional AV_INPUT_BUFFER_PADDING_SIZE at end
 * which is always set to 0.
 *
 * @param size size of extradata
 * @return 0 if OK, AVERROR_xxx on error
 */
int ff_alloc_extradata(AVCodecContext *avctx, int size);

/**
 * Allocate extradata with additional AV_INPUT_BUFFER_PADDING_SIZE at end
 * which is always set to 0 and fill it from pb.
 *
 * @param size size of extradata
 * @return >= 0 if OK, AVERROR_xxx on error
 */
int ff_get_extradata(AVCodecContext *avctx, AVIOContext *pb, int size);

/**
 * add frame for rfps calculation.
 *
 * @param dts timestamp of the i-th frame
 * @return 0 if OK, AVERROR_xxx on error
 */
int ff_rfps_add_frame(AVFormatContext *ic, AVStream *st, int64_t dts);

void ff_rfps_calculate(AVFormatContext *ic);

/**
 * Flags for AVFormatContext.write_uncoded_frame()
 */
enum AVWriteUncodedFrameFlags {

    /**
     * Query whether the feature is possible on this stream.
     * The frame argument is ignored.
     */
    AV_WRITE_UNCODED_FRAME_QUERY           = 0x0001,

};

/**
 * Copies the whilelists from one context to the other
 */
int ff_copy_whitelists(AVFormatContext *dst, AVFormatContext *src);

int ffio_open2_wrapper(struct AVFormatContext *s, AVIOContext **pb, const char *url, int flags,
                       const AVIOInterruptCB *int_cb, AVDictionary **options);

/**
 * Returned by demuxers to indicate that data was consumed but discarded
 * (ignored streams or junk data). The framework will re-call the demuxer.
 */
#define FFERROR_REDO FFERRTAG('R','E','D','O')

#endif /* AVFORMAT_INTERNAL_H */
