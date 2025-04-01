/*
 * Depacketization for RTP Payload Format For AV1 (v1.0)
 * https://aomediacodec.github.io/av1-rtp-spec/
 * Copyright (c) 2024 Axis Communications
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
 * @brief AV1 / RTP depacketization code (RTP Payload Format For AV1 (v1.0))
 * @author Chris Hodges <chris.hodges@axis.com>
 * @note The process will restore TDs and put back size fields into headers.
 *       It will also try to keep complete OBUs and remove partial OBUs
 *       caused by packet drops and thus keep the stream syntactically intact.
 */

#include "libavutil/avstring.h"
#include "libavutil/mem.h"
#include "avformat.h"

#include "rtpdec.h"
#include "libavcodec/av1.h"
#include "rtp_av1.h"

// enable tracing of packet data
//#define RTPDEC_AV1_VERBOSE_TRACE

/**
 * RTP/AV1 specific private data.
 */
struct PayloadContext {
    uint32_t timestamp; ///< last received timestamp for frame
    uint8_t profile;    ///< profile (main/high/professional)
    uint8_t level_idx;  ///< level (0-31)
    uint8_t tier;       ///< main tier or high tier
    uint16_t prev_seq;  ///< sequence number of previous packet
    unsigned int frag_obu_size;     ///< current total size of fragmented OBU
    unsigned int frag_pkt_leb_pos;  ///< offset in buffer where OBU LEB starts
    unsigned int frag_lebs_res;     ///< number of bytes reserved for LEB
    unsigned int frag_header_size;  ///< size of OBU header (1 or 2)
    int needs_td;                   ///< indicates that a TD should be output
    int drop_fragment;              ///< drop all fragments until next frame
    int keyframe_seen;              ///< keyframe was seen
    int wait_for_keyframe;          ///< message about waiting for keyframe has been issued
};

static int sdp_parse_fmtp_config_av1(AVFormatContext *s,
                                     AVStream *stream,
                                     PayloadContext *av1_data,
                                     const char *attr, const char *value) {
    if (!strcmp(attr, "profile")) {
        av1_data->profile = atoi(value);
        av_log(s, AV_LOG_DEBUG, "RTP AV1 profile: %u\n", av1_data->profile);
    } else if (!strcmp(attr, "level-idx")) {
        av1_data->level_idx = atoi(value);
        av_log(s, AV_LOG_DEBUG, "RTP AV1 level: %u\n", av1_data->profile);
    } else if (!strcmp(attr, "tier")) {
        av1_data->tier = atoi(value);
        av_log(s, AV_LOG_DEBUG, "RTP AV1 tier: %u\n", av1_data->tier);
    }
    return 0;
}

// return 0 on complete packet, -1 on partial packet
static int av1_handle_packet(AVFormatContext *ctx, PayloadContext *data,
                             AVStream *st, AVPacket *pkt, uint32_t *timestamp,
                             const uint8_t *buf, int len, uint16_t seq,
                             int flags) {
    uint8_t aggr_hdr;
    int result = 0;
    int is_frag_cont;
    int is_last_fragmented;
    int is_first_pkt;
    unsigned int num_obus;
    unsigned int obu_cnt = 1;
    unsigned int rem_pkt_size = len;
    unsigned int pktpos;
    const uint8_t *buf_ptr = buf;
    uint16_t expected_seq = data->prev_seq + 1;
    int16_t seq_diff = seq - expected_seq;

    data->prev_seq = seq;

    if (!len) {
        av_log(ctx, AV_LOG_ERROR, "Empty AV1 RTP packet\n");
        return AVERROR_INVALIDDATA;
    }
    if (len < 2) {
        av_log(ctx, AV_LOG_ERROR, "AV1 RTP packet too short\n");
        return AVERROR_INVALIDDATA;
    }

    /* The payload structure is supposed to be straight-forward, but there are a
     * couple of edge cases which need to be tackled and make things a bit more
     * complex.
     * These are mainly due to:
     * - To reconstruct the OBU size for fragmented packets and place it the OBU
     *   header, the final size will not be known until the last fragment has
     *   been parsed. However, the number LEBs in the header is variable
     *   depending on the length of the payload.
     * - We are increasing the out-packet size while we are getting fragmented
     *   OBUs. If an RTP packet gets dropped, we would create corrupted OBUs.
     *   In this case we decide to drop the whole frame.
     */

#ifdef RTPDEC_AV1_VERBOSE_TRACE
    av_log(ctx, AV_LOG_TRACE, "RTP Packet %d in (%x), len=%d:\n",
           seq, flags, len);
    av_hex_dump_log(ctx, AV_LOG_TRACE, buf, FFMIN(len, 64));
    av_log(ctx, AV_LOG_TRACE, "... end at offset %x:\n", FFMAX(len - 64, 0));
    av_hex_dump_log(ctx, AV_LOG_TRACE, buf + FFMAX(len - 64, 0), FFMIN(len - 64, 64));
#endif

    /* 8 bit aggregate header: Z Y W W N - - - */
    aggr_hdr = *buf_ptr++;
    rem_pkt_size--;

    /* Z: MUST be set to 1 if the first OBU element is an OBU fragment that is a
     * continuation of an OBU fragment from the previous packet, and MUST be set
     * to 0 otherwise */
    is_frag_cont = (aggr_hdr >> AV1B_AGGR_HDR_FRAG_CONT) & 1;

    /* Y: MUST be set to 1 if the last OBU element is an OBU fragment that will
     * continue in the next packet, and MUST be set to 0 otherwise */
    is_last_fragmented = (aggr_hdr >> AV1B_AGGR_HDR_LAST_FRAG) & 1;

    /* W: two bit field that describes the number of OBU elements in the packet.
     * This field MUST be set equal to 0 or equal to the number of OBU elements
     * contained in the packet.
     * If set to 0, each OBU element MUST be preceded by a length field.
     * If not set to 0 (i.e., W = 1, 2 or 3) the last OBU element MUST NOT be
     * preceded by a length field (it's derived from RTP packet size minus other
     * known lengths). */
    num_obus = (aggr_hdr >> AV1S_AGGR_HDR_NUM_OBUS) & AV1M_AGGR_HDR_NUM_OBUS;

    /* N: MUST be set to 1 if the packet is the first packet of a coded video
     * sequence, and MUST be set to 0 otherwise.*/
    is_first_pkt = (aggr_hdr >> AV1B_AGGR_HDR_FIRST_PKT) & 1;

    if (is_frag_cont) {
        if (data->drop_fragment) {
            return AVERROR_INVALIDDATA;
        }
        if (is_first_pkt) {
            av_log(ctx, AV_LOG_ERROR, "Illegal aggregation header in first AV1 RTP packet\n");
            return AVERROR_INVALIDDATA;
        }
        if (seq_diff) {
            av_log(ctx, AV_LOG_WARNING, "AV1 RTP frag packet sequence mismatch (%d != %d), dropping temporal unit\n",
                   seq, expected_seq);
            goto drop_fragment;
        }
        if (!pkt->size || !data->frag_obu_size) {
            av_log(ctx, AV_LOG_WARNING, "Unexpected fragment continuation in AV1 RTP packet\n");
            goto drop_fragment; // avoid repeated output for the same fragment
        }
    } else {
        if (!is_first_pkt && !data->keyframe_seen) {
            if (!data->wait_for_keyframe) {
                data->wait_for_keyframe = 1;
                av_log(ctx, AV_LOG_WARNING, "AV1 RTP packet before keyframe, dropping and waiting for next keyframe\n");
            }
            goto drop_fragment;
        }
        if (seq_diff && !is_first_pkt) {
            av_log(ctx, AV_LOG_WARNING, "AV1 RTP unfrag packet sequence mismatch (%d != %d), dropping temporal unit\n",
                   seq, expected_seq);
            goto drop_fragment;
        }
        data->drop_fragment = 0;
        if (!data->needs_td && ((data->timestamp != *timestamp) || is_first_pkt)) {
            av_log(ctx, AV_LOG_TRACE, "Timestamp changed to %u (or first pkt %d), forcing TD\n", *timestamp, is_first_pkt);
            data->needs_td = 1;
            data->frag_obu_size = 0; // new temporal unit might have been caused by dropped packets
        }
        if (data->frag_obu_size) {
            data->frag_obu_size = 0; // make sure we recover
            av_log(ctx, AV_LOG_ERROR, "Missing fragment continuation in AV1 RTP packet\n");
            return AVERROR_INVALIDDATA;
        }
        // update the timestamp in the frame packet with the one from the RTP packet
        data->timestamp = *timestamp;
    }
    pktpos = pkt->size;

#ifdef RTPDEC_AV1_VERBOSE_TRACE
    av_log(ctx, AV_LOG_TRACE, "Input buffer size %d, aggr head 0x%02x fc %d, lf %d, no %d, fp %d\n",
           len, aggr_hdr, is_frag_cont, is_last_fragmented, num_obus, is_first_pkt);
#endif

    if (is_first_pkt) {
        pkt->flags |= AV_PKT_FLAG_KEY;
        data->keyframe_seen = 1;
        data->wait_for_keyframe = 0;
    }

    // loop over OBU elements
    while (rem_pkt_size) {
        uint32_t obu_size;
        int num_lebs;
        int needs_size_field;
        int output_size;
        unsigned int obu_payload_size;
        uint8_t obu_hdr;

        obu_size = rem_pkt_size;
        if (!num_obus || obu_cnt < num_obus) {
            // read out explicit OBU element size (which almost corresponds to the original OBU size)
            num_lebs = parse_leb(ctx, buf_ptr, rem_pkt_size, &obu_size);
            if (!num_lebs) {
                return AVERROR_INVALIDDATA;
            }
            rem_pkt_size -= num_lebs;
            buf_ptr += num_lebs;
        }
        // read first byte (which is the header byte only for non-fragmented elements)
        obu_hdr = *buf_ptr;
        if (obu_size > rem_pkt_size) {
            av_log(ctx, AV_LOG_ERROR, "AV1 OBU size %u larger than remaining pkt size %d\n", obu_size, rem_pkt_size);
            return AVERROR_INVALIDDATA;
        }

        if (!obu_size) {
            av_log(ctx, AV_LOG_ERROR, "Unreasonable AV1 OBU size %u\n", obu_size);
            return AVERROR_INVALIDDATA;
        }

        if (!is_frag_cont) {
            uint8_t obu_type = (obu_hdr >> AV1S_OBU_TYPE) & AV1M_OBU_TYPE;
            if (obu_hdr & AV1F_OBU_FORBIDDEN) {
                av_log(ctx, AV_LOG_ERROR, "Forbidden bit set in AV1 OBU header (0x%02x)\n", obu_hdr);
                return AVERROR_INVALIDDATA;
            }
            // ignore and remove OBUs according to spec
            if ((obu_type == AV1_OBU_TEMPORAL_DELIMITER) ||
                (obu_type == AV1_OBU_TILE_LIST)) {
                pktpos += obu_size;
                rem_pkt_size -= obu_size;
                // TODO: This probably breaks if the OBU_TILE_LIST is fragmented
                // into the next RTP packet, so at least check and fail here
                if (rem_pkt_size == 0 && is_last_fragmented) {
                    avpriv_report_missing_feature(ctx, "AV1 OBU_TILE_LIST (should not be there!) to be ignored but is fragmented\n");
                    return AVERROR_PATCHWELCOME;
                }
                obu_cnt++;
                continue;
            }
        }

        // If we need to add a size field, out size will be different
        output_size = obu_size;
        // Spec says the OBUs should have their size fields removed,
        // but this is not mandatory
        if (is_frag_cont || (obu_hdr & AV1F_OBU_HAS_SIZE_FIELD)) {
            needs_size_field = 0;
        } else {
            needs_size_field = 1;
            // (re)calculate number of LEB bytes needed (if it was implicit, there were no LEBs)
            output_size += calc_leb_size(obu_size - (1 + ((obu_hdr & AV1F_OBU_EXTENSION_FLAG) ? 1 : 0)));
        }

        if (!is_frag_cont && (obu_cnt == 1)) {
            if (data->needs_td) {
                output_size += 2; // for Temporal Delimiter (TD)
            }
            if (pkt->data) {
                if ((result = av_grow_packet(pkt, output_size)) < 0)
                    return result;
            } else {
                if ((result = av_new_packet(pkt, output_size) < 0))
                    return result;
            }

            if (data->needs_td) {
                // restore TD
                pkt->data[pktpos++] = 0x12;
                pkt->data[pktpos++] = 0x00;
            }
            data->needs_td = 0;
        } else {
            if ((result = av_grow_packet(pkt, output_size)) < 0)
                return result;
        }

        obu_payload_size = obu_size;
        // do we need to restore the OBU size field?
        if (needs_size_field) {
            // set obu_has_size_field in header byte
            pkt->data[pktpos++] = *buf_ptr++ | AV1F_OBU_HAS_SIZE_FIELD;
            data->frag_header_size = 1;
            obu_payload_size--;

            // copy extension byte, if available
            if (obu_hdr & AV1F_OBU_EXTENSION_FLAG) {
                /* TODO we cannot handle the edge case where last element is a
                 * fragment of exactly one byte AND the header has the extension
                 * flag set. Note that it would be more efficient to not send a
                 * fragment of one byte and instead drop the size field of the
                 * prior element */
                if (!obu_payload_size) {
                    av_log(ctx, AV_LOG_ERROR, "AV1 OBU too short for extension byte (0x%02x)\n",
                           obu_hdr);
                    return AVERROR_INVALIDDATA;
                }
                pkt->data[pktpos++] = *buf_ptr++;
                data->frag_header_size = 2;
                obu_payload_size--;
            }

            // remember start position of LEB for possibly fragmented packet to
            // fixup OBU size later
            data->frag_pkt_leb_pos = pktpos;
            // write intermediate OBU size field
            num_lebs = write_leb(pkt->data + pktpos, obu_payload_size);
            data->frag_lebs_res = num_lebs;
            pktpos += num_lebs;
        }
        // copy verbatim or without above header size patch
        memcpy(pkt->data + pktpos, buf_ptr, obu_payload_size);
        pktpos += obu_payload_size;
        buf_ptr += obu_payload_size;
        rem_pkt_size -= obu_size;

        // if we were handling a fragmented packet and this was the last
        // fragment, correct OBU size field
        if (data->frag_obu_size && (rem_pkt_size || !is_last_fragmented)) {
            uint32_t final_obu_size = data->frag_obu_size + obu_size - data->frag_header_size;
            uint8_t *lebptr = pkt->data + data->frag_pkt_leb_pos;
            num_lebs = calc_leb_size(final_obu_size);

            // check if we had allocated enough LEB bytes in header,
            // otherwise make some extra space
            if (num_lebs > data->frag_lebs_res) {
                int extra_bytes = num_lebs - data->frag_lebs_res;
                if ((result = av_grow_packet(pkt, extra_bytes)) < 0)
                    return result;
                // update pointer in case buffer address changed
                lebptr = pkt->data + data->frag_pkt_leb_pos;
                // move existing data for OBU back a bit
                memmove(lebptr + extra_bytes, lebptr,
                        pkt->size - extra_bytes - data->frag_pkt_leb_pos);
                // move pktpos further down for following OBUs in same packet.
                pktpos += extra_bytes;
            }

            // update OBU size field
            write_leb(lebptr, final_obu_size);

            data->frag_obu_size = 0; // signal end of fragment
        } else if (is_last_fragmented && !rem_pkt_size) {
            // add to total OBU size, so we can fix that in OBU header
            // (but only if the OBU size was missing!)
            if (needs_size_field || data->frag_obu_size) {
                data->frag_obu_size += obu_size;
            }
            // fragment not yet finished!
            result = -1;
        }
        is_frag_cont = 0;

        if (!rem_pkt_size && num_obus && (num_obus != obu_cnt)) {
            av_log(ctx, AV_LOG_WARNING, "AV1 aggregation header indicated %u OBU elements, was %u\n",
                   num_obus, obu_cnt);
        }
        obu_cnt++;
    }

    if (flags & RTP_FLAG_MARKER) {
        av_log(ctx, AV_LOG_TRACE, "TD on next packet due to marker\n");
        data->needs_td = 1;
    } else {
        // fragment may be complete, but temporal unit is not yet finished
        result = -1;
    }

    if (!is_last_fragmented) {
        data->frag_obu_size = 0;
        data->frag_pkt_leb_pos = 0;
    }

#ifdef RTPDEC_AV1_VERBOSE_TRACE
    if (!result) {
        av_log(ctx, AV_LOG_TRACE, "AV1 out pkt-size: %d\n", pkt->size);
        av_hex_dump_log(ctx, AV_LOG_TRACE, pkt->data, FFMIN(pkt->size, 64));
        av_log(ctx, AV_LOG_TRACE, "... end at offset %x:\n", FFMAX(pkt->size - 64, 0));
        av_hex_dump_log(ctx, AV_LOG_TRACE, pkt->data + FFMAX(pkt->size - 64, 0), FFMIN(pkt->size, 64));
    }
#endif
    pkt->stream_index = st->index;

    return result;

drop_fragment:
    data->keyframe_seen = 0;
    data->drop_fragment = 1;
    data->frag_obu_size = 0;
    data->needs_td = 1;
    if (pkt->size) {
        av_log(ctx, AV_LOG_TRACE, "Dumping current AV1 frame packet\n");
        // we can't seem to deallocate the fragmented packet, but we can shrink it to 0
        av_shrink_packet(pkt, 0);
    }
    return AVERROR_INVALIDDATA;
}

static void av1_close_context(PayloadContext *data) {
}

static int av1_need_keyframe(PayloadContext *data)
{
    return !data->keyframe_seen;
}

static int parse_av1_sdp_line(AVFormatContext *s, int st_index,
                              PayloadContext *av1_data, const char *line) {
    AVStream * stream;
    const char *p = line;
    int result = 0;

    if (st_index < 0)
        return 0;

    stream = s->streams[st_index];

    /* Optional parameters are profile, level-idx, and tier.
     * See Section 7.2.1 of https://aomediacodec.github.io/av1-rtp-spec/ */
    if (av_strstart(p, "fmtp:", &p)) {
        result = ff_parse_fmtp(s, stream, av1_data, p, sdp_parse_fmtp_config_av1);
        av_log(s, AV_LOG_DEBUG, "RTP AV1 Profile: %u, Level: %u, Tier: %u\n",
               av1_data->profile, av1_data->level_idx, av1_data->tier);
    }

    return result;
}

const RTPDynamicProtocolHandler ff_av1_dynamic_handler = {
        .enc_name         = "AV1",
        .codec_type       = AVMEDIA_TYPE_VIDEO,
        .codec_id         = AV_CODEC_ID_AV1,
        .need_parsing     = AVSTREAM_PARSE_FULL,
        .priv_data_size   = sizeof(PayloadContext),
        .parse_sdp_a_line = parse_av1_sdp_line,
        .close            = av1_close_context,
        .parse_packet     = av1_handle_packet,
        .need_keyframe    = av1_need_keyframe,
};
