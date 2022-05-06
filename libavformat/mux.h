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

#ifndef AVFORMAT_MUX_H
#define AVFORMAT_MUX_H

#include <stdint.h>
#include "libavcodec/packet.h"
#include "avformat.h"

/**
 * Add packet to an AVFormatContext's packet_buffer list, determining its
 * interleaved position using compare() function argument.
 * @return 0 on success, < 0 on error. pkt will always be blank on return.
 */
int ff_interleave_add_packet(AVFormatContext *s, AVPacket *pkt,
                             int (*compare)(AVFormatContext *, const AVPacket *, const AVPacket *));

/**
 * Interleave an AVPacket per dts so it can be muxed.
 * See the documentation of AVOutputFormat.interleave_packet for details.
 */
int ff_interleave_packet_per_dts(AVFormatContext *s, AVPacket *pkt,
                                 int flush, int has_packet);

/**
 * Interleave packets directly in the order in which they arrive
 * without any sort of buffering.
 */
int ff_interleave_packet_passthrough(AVFormatContext *s, AVPacket *pkt,
                                     int flush, int has_packet);

/**
 * Find the next packet in the interleaving queue for the given stream.
 *
 * @return a pointer to a packet if one was found, NULL otherwise.
 */
const AVPacket *ff_interleaved_peek(AVFormatContext *s, int stream);

int ff_get_muxer_ts_offset(AVFormatContext *s, int stream_index, int64_t *offset);

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
 * Write a packet to another muxer than the one the user originally
 * intended. Useful when chaining muxers, where one muxer internally
 * writes a received packet to another muxer.
 *
 * @param dst the muxer to write the packet to
 * @param dst_stream the stream index within dst to write the packet to
 * @param pkt the packet to be written. It will be returned blank when
 *            av_interleaved_write_frame() is used, unchanged otherwise.
 * @param src the muxer the packet originally was intended for
 * @param interleave 0->use av_write_frame, 1->av_interleaved_write_frame
 * @return the value av_write_frame returned
 */
int ff_write_chained(AVFormatContext *dst, int dst_stream, AVPacket *pkt,
                     AVFormatContext *src, int interleave);

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
 * Make shift_size amount of space at read_start by shifting data in the output
 * at read_start until the current IO position. The underlying IO context must
 * be seekable.
 */
int ff_format_shift_data(AVFormatContext *s, int64_t read_start, int shift_size);

/**
 * Utility function to open IO stream of output format.
 *
 * @param s AVFormatContext
 * @param url URL or file name to open for writing
 * @options optional options which will be passed to io_open callback
 * @return >=0 on success, negative AVERROR in case of failure
 */
int ff_format_output_open(AVFormatContext *s, const char *url, AVDictionary **options);

/**
 * Copy encoding parameters from source to destination stream
 *
 * @param dst pointer to destination AVStream
 * @param src pointer to source AVStream
 * @return >=0 on success, AVERROR code on error
 */
int ff_stream_encode_params_copy(AVStream *dst, const AVStream *src);

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

#endif /* AVFORMAT_MUX_H */
