/*
 * Ogg muxer
 * Copyright (c) 2007 Baptiste Coudurier <baptiste dot coudurier at free dot fr>
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

#ifndef AVFORMAT_OGG_MUXER_H
#define AVFORMAT_OGG_MUXER_H

#include <stdint.h>

#include "libavutil/dict.h"
#include "libavformat/avformat.h"

#define MAX_PAGE_SIZE 65025

typedef struct OGGPage {
    int64_t start_granule;
    int64_t granule;
    int stream_index;
    uint8_t flags;
    uint8_t segments_count;
    uint8_t segments[255];
    uint8_t data[MAX_PAGE_SIZE];
    uint16_t size;
} OGGPage;

typedef struct OGGMuxerMapping OGGMuxerMapping;

typedef struct OGGStreamContext {
    unsigned page_counter;
    uint8_t *header[3];
    int header_len[3];
    const OGGMuxerMapping *mapping;
    void *priv_data; ///< state of the mapping
    /**
     * Granule layout, set by the mapping. The timestamp of a granule is its
     * high part, above granule_shift bits, plus its granule_delta_mask bits.
     */
    unsigned granule_shift;
    int64_t granule_delta_mask;
    int64_t granule_key_mask;  ///< bits all clear on keyframes, 0 when no granule marks one
    int packet_granule_offset; ///< added to the end timestamp of a packet without packet_granule
    int eos;
    unsigned page_count; ///< number of page buffered
    OGGPage page; ///< current page
    unsigned serial_num; ///< serial number
    int64_t last_granule; ///< last packet granule
    int packet_seen; ///< true when packets have been submitted
} OGGStreamContext;

/**
 * Codec mapping of the muxer: the parts of the Ogg encapsulation that depend
 * on the codec.
 */
struct OGGMuxerMapping {
    enum AVCodecID codec_id;
    size_t priv_data_size;  ///< size of OGGStreamContext.priv_data
    int optional_extradata; ///< the headers can be built without extradata
    int new_links;          ///< a new link can replace the current one
    /**
     * Build the header packets in OGGStreamContext.header, adjusting the
     * stream time base when the codec sets its own.
     */
    int (*init)(AVFormatContext *s, AVStream *st);
    /**
     * @return why a new link cannot start with this packet, NULL if it can.
     *         When the callback is NULL, the codec imposes no additional
     *         packet restriction.
     */
    const char *(*new_link_refused)(const AVStream *st, const AVPacket *pkt,
                                    const uint8_t *new_extradata, size_t size);
    /**
     * @return the granule of a packet. When NULL, the granule is the
     *         packet's end timestamp plus OGGStreamContext.packet_granule_offset.
     */
    int64_t (*packet_granule)(AVStream *st, const AVPacket *pkt);
};

/**
 * Allocate a comment header packet holding a VorbisComment. The metadata is
 * converted to VorbisComment field names in place.
 *
 * @param offset      the number of zeroed bytes before the VorbisComment, left
 *                    for the codec's packet type
 * @param header_len  set to the packet size
 * @param framing_bit whether the packet ends with a framing bit
 * @return the packet, to free with av_free(), or NULL on error
 */
uint8_t *ff_ogg_write_vorbiscomment(int64_t offset, int bitexact,
                                    int *header_len, AVDictionary **m, int framing_bit,
                                    AVChapter **chapters, unsigned int nb_chapters);

/**
 * Build the headers of a codec using the Xiph header layout: identification,
 * comment and setup packets laced in the extradata.
 *
 * @param first_header_size the size of the identification header
 * @param comment_type      the packet type of the comment header
 * @param name              the codec name in the comment header
 * @param framing_bit       whether the comment header ends with a framing bit
 */
int ff_ogg_build_xiph_headers(AVFormatContext *s, AVStream *st,
                              int first_header_size, int comment_type,
                              const char *name, int framing_bit);

extern const OGGMuxerMapping ff_ogg_flac_muxer_mapping;
extern const OGGMuxerMapping ff_ogg_opus_muxer_mapping;
extern const OGGMuxerMapping ff_ogg_speex_muxer_mapping;
extern const OGGMuxerMapping ff_ogg_theora_muxer_mapping;
extern const OGGMuxerMapping ff_ogg_vorbis_muxer_mapping;
extern const OGGMuxerMapping ff_ogg_vp8_muxer_mapping;

#endif /* AVFORMAT_OGG_MUXER_H */
