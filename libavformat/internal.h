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

#include "libavutil/bprint.h"
#include "avformat.h"
#include "os_support.h"

#define MAX_URL_SIZE 4096

/** size of probe buffer, for guessing file type from file contents */
#define PROBE_BUF_MIN 2048
#define PROBE_BUF_MAX (1 << 20)

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

    /**
     * Timestamp of the end of the shortest stream.
     */
    int64_t shortest_end;

    /**
     * Whether or not avformat_init_output has already been called
     */
    int initialized;

    /**
     * Whether or not avformat_init_output fully initialized streams
     */
    int streams_initialized;

    /**
     * ID3v2 tag useful for MP3 demuxing
     */
    AVDictionary *id3v2_meta;

    /*
     * Prefer the codec framerate for avg_frame_rate computation.
     */
    int prefer_codec_framerate;
};

struct AVStreamInternal {
    /**
     * Set to 1 if the codec allows reordering, so pts can be different
     * from dts.
     */
    int reorder;

    /**
     * bitstream filters to run on stream
     * - encoding: Set by muxer using ff_stream_add_bitstream_filter
     * - decoding: unused
     */
    AVBSFContext **bsfcs;
    int nb_bsfcs;

    /**
     * Whether or not check_bitstream should still be run on each packet
     */
    int bitstream_checked;

    /**
     * The codec context used by avformat_find_stream_info, the parser, etc.
     */
    AVCodecContext *avctx;
    /**
     * 1 if avctx has been initialized with the values from the codec parameters
     */
    int avctx_inited;

    enum AVCodecID orig_codec_id;

    /* the context for extracting extradata in find_stream_info()
     * inited=1/bsf=NULL signals that extracting is not possible (codec not
     * supported) */
    struct {
        AVBSFContext *bsf;
        AVPacket     *pkt;
        int inited;
    } extract_extradata;

    /**
     * Whether the internal avctx needs to be updated from codecpar (after a late change to codecpar)
     */
    int need_context_update;

    FFFrac *priv_pts;
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

/**
 * Automatically create sub-directories
 *
 * @param path will create sub-directories by path
 * @return 0, or < 0 on error
 */
int ff_mkdir_p(const char *path);

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
                             int (*compare)(AVFormatContext *, const AVPacket *, const AVPacket *));

void ff_read_frame_flush(AVFormatContext *s);

#define NTP_OFFSET 2208988800ULL
#define NTP_OFFSET_US (NTP_OFFSET * 1000000ULL)

/** Get the current time since NTP epoch in microseconds. */
uint64_t ff_ntp_time(void);

/**
 * Get the NTP time stamp formatted as per the RFC-5905.
 *
 * @param ntp_time NTP time in micro seconds (since NTP epoch)
 * @return the formatted NTP time stamp
 */
uint64_t ff_get_formatted_ntp_time(uint64_t ntp_time_us);

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

/**
 * Same as ff_get_line but strip the white-space characters in the text tail
 *
 * @param s the read-only AVIOContext
 * @param buf buffer to store the read line
 * @param maxlen size of the buffer
 * @return the length of the string written in the buffer
 */
int ff_get_chomp_line(AVIOContext *s, char *buf, int maxlen);

/**
 * Read a whole line of text from AVIOContext to an AVBPrint buffer. Stop
 * reading after reaching a \\r, a \\n, a \\r\\n, a \\0 or EOF.  The line
 * ending characters are NOT included in the buffer, but they are skipped on
 * the input.
 *
 * @param s the read-only AVIOContext
 * @param bp the AVBPrint buffer
 * @return the length of the read line, not including the line endings,
 *         negative on error.
 */
int64_t ff_read_line_to_bprint(AVIOContext *s, AVBPrint *bp);

/**
 * Read a whole line of text from AVIOContext to an AVBPrint buffer overwriting
 * its contents. Stop reading after reaching a \\r, a \\n, a \\r\\n, a \\0 or
 * EOF. The line ending characters are NOT included in the buffer, but they
 * are skipped on the input.
 *
 * @param s the read-only AVIOContext
 * @param bp the AVBPrint buffer
 * @return the length of the read line not including the line endings,
 *         negative on error, or if the buffer becomes truncated.
 */
int64_t ff_read_line_to_bprint_overwrite(AVIOContext *s, AVBPrint *bp);

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
 * Add a bitstream filter to a stream.
 *
 * @param st output stream to add a filter to
 * @param name the name of the filter to add
 * @param args filter-specific argument string
 * @return  >0 on success;
 *          AVERROR code on failure
 */
int ff_stream_add_bitstream_filter(AVStream *st, const char *name, const char *args);

/**
 * Copy encoding parameters from source to destination stream
 *
 * @param dst pointer to destination AVStream
 * @param src pointer to source AVStream
 * @return >=0 on success, AVERROR code on error
 */
int ff_stream_encode_params_copy(AVStream *dst, const AVStream *src);

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
        if (logctx) {
            char err[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_make_error_string(err, AV_ERROR_MAX_STRING_SIZE, ret);
            av_log(logctx, AV_LOG_ERROR, "failed to rename file %s to %s: %s\n", oldpath, newpath, err);
        }
    }
    return ret;
}

/**
 * Allocate extradata with additional AV_INPUT_BUFFER_PADDING_SIZE at end
 * which is always set to 0.
 *
 * Previously allocated extradata in par will be freed.
 *
 * @param size size of extradata
 * @return 0 if OK, AVERROR_xxx on error
 */
int ff_alloc_extradata(AVCodecParameters *par, int size);

/**
 * Allocate extradata with additional AV_INPUT_BUFFER_PADDING_SIZE at end
 * which is always set to 0 and fill it from pb.
 *
 * @param size size of extradata
 * @return >= 0 if OK, AVERROR_xxx on error
 */
int ff_get_extradata(AVFormatContext *s, AVCodecParameters *par, AVIOContext *pb, int size);

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
int ff_copy_whiteblacklists(AVFormatContext *dst, const AVFormatContext *src);

/**
 * Returned by demuxers to indicate that data was consumed but discarded
 * (ignored streams or junk data). The framework will re-call the demuxer.
 */
#define FFERROR_REDO FFERRTAG('R','E','D','O')

/**
 * Utility function to open IO stream of output format.
 *
 * @param s AVFormatContext
 * @param url URL or file name to open for writing
 * @options optional options which will be passed to io_open callback
 * @return >=0 on success, negative AVERROR in case of failure
 */
int ff_format_output_open(AVFormatContext *s, const char *url, AVDictionary **options);

/*
 * A wrapper around AVFormatContext.io_close that should be used
 * instead of calling the pointer directly.
 */
void ff_format_io_close(AVFormatContext *s, AVIOContext **pb);

/**
 * Utility function to check if the file uses http or https protocol
 *
 * @param s AVFormatContext
 * @param filename URL or file name to open for writing
 */
int ff_is_http_proto(char *filename);

/**
 * Parse creation_time in AVFormatContext metadata if exists and warn if the
 * parsing fails.
 *
 * @param s AVFormatContext
 * @param timestamp parsed timestamp in microseconds, only set on successful parsing
 * @param return_seconds set this to get the number of seconds in timestamp instead of microseconds
 * @return 1 if OK, 0 if the metadata was not present, AVERROR(EINVAL) on parse error
 */
int ff_parse_creation_time_metadata(AVFormatContext *s, int64_t *timestamp, int return_seconds);

/**
 * Standardize creation_time metadata in AVFormatContext to an ISO-8601
 * timestamp string.
 *
 * @param s AVFormatContext
 * @return <0 on error
 */
int ff_standardize_creation_time(AVFormatContext *s);

#define CONTAINS_PAL 2
/**
 * Reshuffles the lines to use the user specified stride.
 *
 * @param ppkt input and output packet
 * @return negative error code or
 *         0 if no new packet was allocated
 *         non-zero if a new packet was allocated and ppkt has to be freed
 *         CONTAINS_PAL if in addition to a new packet the old contained a palette
 */
int ff_reshuffle_raw_rgb(AVFormatContext *s, AVPacket **ppkt, AVCodecParameters *par, int expected_stride);

/**
 * Retrieves the palette from a packet, either from side data, or
 * appended to the video data in the packet itself (raw video only).
 * It is commonly used after a call to ff_reshuffle_raw_rgb().
 *
 * Use 0 for the ret parameter to check for side data only.
 *
 * @param pkt pointer to packet before calling ff_reshuffle_raw_rgb()
 * @param ret return value from ff_reshuffle_raw_rgb(), or 0
 * @param palette pointer to palette buffer
 * @return negative error code or
 *         1 if the packet has a palette, else 0
 */
int ff_get_packet_palette(AVFormatContext *s, AVPacket *pkt, int ret, uint32_t *palette);

/**
 * Finalize buf into extradata and set its size appropriately.
 */
int ff_bprint_to_codecpar_extradata(AVCodecParameters *par, struct AVBPrint *buf);

/**
 * Find the next packet in the interleaving queue for the given stream.
 * The pkt parameter is filled in with the queued packet, including
 * references to the data (which the caller is not allowed to keep or
 * modify).
 *
 * @return 0 if a packet was found, a negative value if no packet was found
 */
int ff_interleaved_peek(AVFormatContext *s, int stream,
                        AVPacket *pkt, int add_offset);


int ff_lock_avformat(void);
int ff_unlock_avformat(void);

/**
 * Set AVFormatContext url field to the provided pointer. The pointer must
 * point to a valid string. The existing url field is freed if necessary. Also
 * set the legacy filename field to the same string which was provided in url.
 */
void ff_format_set_url(AVFormatContext *s, char *url);

#define FF_PACKETLIST_FLAG_REF_PACKET (1 << 0) /**< Create a new reference for the packet instead of
                                                    transferring the ownership of the existing one to the
                                                    list. */

/**
 * Append an AVPacket to the list.
 *
 * @param head  List head element
 * @param tail  List tail element
 * @param pkt   The packet being appended. The data described in it will
 *              be made reference counted if it isn't already.
 * @param flags Any combination of FF_PACKETLIST_FLAG_* flags
 * @return 0 on success, negative AVERROR value on failure. On failure,
           the list is unchanged
 */
int ff_packet_list_put(AVPacketList **head, AVPacketList **tail,
                       AVPacket *pkt, int flags);

/**
 * Remove the oldest AVPacket in the list and return it.
 * The behaviour is undefined if the packet list is empty.
 *
 * @note The pkt will be overwritten completely. The caller owns the
 *       packet and must unref it by itself.
 *
 * @param head List head element
 * @param tail List tail element
 * @param pkt  Pointer to an AVPacket struct
 * @return 0 on success. Success is guaranteed
 *         if the packet list is not empty.
 */
int ff_packet_list_get(AVPacketList **head, AVPacketList **tail,
                       AVPacket *pkt);

/**
 * Wipe the list and unref all the packets in it.
 *
 * @param head List head element
 * @param tail List tail element
 */
void ff_packet_list_free(AVPacketList **head, AVPacketList **tail);

void avpriv_register_devices(const AVOutputFormat * const o[], const AVInputFormat * const i[]);

#endif /* AVFORMAT_INTERNAL_H */
