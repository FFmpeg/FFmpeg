/*
 * Packetization for RTP Payload Format For AV1 (v1.0)
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
 * @brief AV1 / RTP packetization code (RTP Payload Format For AV1 (v1.0))
 * @author Chris Hodges <chris.hodges@axis.com>
 * @note This will remove TDs and OBU size fields
 */

#include "avformat.h"
#include "rtpenc.h"
#include "libavcodec/av1.h"
#include "rtp_av1.h"

// enable tracing of packet data
//#define RTPENC_AV1_VERBOSE_TRACE

// enable searching for sequence header as workaround for AV1 encoders
// that do not set AV_PKT_FLAG_KEY correctly
#define RTPENC_AV1_SEARCH_SEQ_HEADER 1

void ff_rtp_send_av1(AVFormatContext *ctx, const uint8_t *frame_buf, int frame_size, int is_keyframe) {
    uint8_t aggr_hdr = 0;
    int last_packet_of_frame = 0;
    RTPMuxContext *rtp_ctx = ctx->priv_data;
    const uint8_t *obu_ptr = frame_buf;
    int start_new_packet = 0;
    unsigned int num_obus = 0;
    unsigned int rem_pkt_size = rtp_ctx->max_payload_size - 1;
    uint8_t *pkt_ptr = NULL;

    const uint8_t *curr_obu_ptr = NULL;
    uint32_t curr_elem_size = 0;
    int curr_obu_hdr = -1;
    int curr_obu_ext = -1;
    const uint8_t *last_obu_ptr = NULL;
    uint32_t last_elem_size = 0;
    int last_obu_hdr = -1;
    int last_obu_ext = -1;

    rtp_ctx->timestamp = rtp_ctx->cur_timestamp;

    /* The payload structure is supposed to be straight-forward, but there are a
     * couple of edge cases to be tackled and make things very complex.
     * These are mainly due to:
     * - the OBU element size being optional for the last element, but MANDATORY
     *   if there are more than 3 elements
     * - the size field of the element is made up of a variable number of
     *   LEB bytes
     * - the latter in combination with the desire to fill the max packet size
     *   could cause a catch22
     * - if there's less than 2 bytes remaining (depending on the required LEB),
     *   one would not have space for the payload of an element and must instead
     *   start the next packet
     * - if there's less than 3 bytes remaining, the header byte plus the
     *   optional extension byte will not fit in the fragment making the
     *   handling even more complicated
     * - as some OBU types are supposed to be filtered out, it is hard to decide
     *   via the remaining length whether the outputted OBU element will
     *   actually be the last one
     *
     * There are two major ways to tackle that: Pre-parsing of all OBUs within a
     * frame (adds memory complexity) or lazy copying of the prior element.
     * Here, the latter is implemented.
     */

    if (is_keyframe) {
#if RTPENC_AV1_SEARCH_SEQ_HEADER
        /* search for OBU_SEQUENCE_HEADER to get a better indication that
         * the frame was marked as keyframe is really a KEY_FRAME and not
         * a INTRA_ONLY frame. This might be unnecessary if the AV1 parser/
         * encoder always correctly specifies AV_PKT_FLAG_KEY.
         *
         * Note: Spec does NOT prohibit resending bit-identical
         * OBU_SEQUENCE_HEADER for ANY kind of frame, though!
         */
        int rem_size = frame_size;
        const uint8_t *buf_ptr = frame_buf;
        while (rem_size > 0) {
            uint32_t obu_size;
            uint8_t obu_hdr = *buf_ptr++;
            uint8_t obu_type = (obu_hdr >> AV1S_OBU_TYPE) & AV1M_OBU_TYPE;
            int num_lebs;

            if (obu_type == AV1_OBU_SEQUENCE_HEADER) {
                av_log(ctx, AV_LOG_DEBUG, "Marking FIRST packet\n");
                aggr_hdr |= AV1F_AGGR_HDR_FIRST_PKT;
                break;
            }
            if (!(obu_hdr & AV1F_OBU_HAS_SIZE_FIELD)) {
                break;
            }
            rem_size--;
            // read out explicit OBU size
            num_lebs = parse_leb(ctx, buf_ptr, rem_size, &obu_size);
            if (!num_lebs) {
                break;
            }
            buf_ptr += num_lebs + obu_size;
            rem_size -= num_lebs + obu_size;
        }
#else // RTPENC_AV1_SEARCH_SEQ_HEADER
        av_log(ctx, AV_LOG_DEBUG, "Marking FIRST packet\n");
        aggr_hdr |= AV1F_AGGR_HDR_FIRST_PKT;
#endif // RTPENC_AV1_SEARCH_SEQ_HEADER
    }
    rem_pkt_size = rtp_ctx->max_payload_size - 1;
    pkt_ptr = rtp_ctx->buf + 1;

#ifdef RTPENC_AV1_VERBOSE_TRACE
    av_log(ctx, AV_LOG_TRACE, "AV1 Frame %d in (%x), size=%d:\n",
           rtp_ctx->seq, rtp_ctx->flags, frame_size);
    av_hex_dump_log(ctx, AV_LOG_TRACE, frame_buf, FFMIN(frame_size, 128));
#endif

    while (frame_size) {
        uint32_t obu_size;
        int num_lebs = 0;
        int ext_byte = -1;

        uint8_t obu_hdr = *obu_ptr++;
        uint8_t obu_type = (obu_hdr >> AV1S_OBU_TYPE) & AV1M_OBU_TYPE;
        frame_size--;

        if (obu_hdr & AV1F_OBU_FORBIDDEN) {
            av_log(ctx, AV_LOG_ERROR, "Forbidden bit set in AV1 OBU header (0x%02x)\n", obu_hdr);
            return;
        }

        if (obu_hdr & AV1F_OBU_EXTENSION_FLAG) {
            if (!frame_size) {
                av_log(ctx, AV_LOG_ERROR, "Out of data for AV1 OBU header extension byte\n");
                return;
            }
            ext_byte = *obu_ptr++;
            frame_size--;
        }

        if (obu_hdr & AV1F_OBU_HAS_SIZE_FIELD) {
            obu_hdr &= ~AV1F_OBU_HAS_SIZE_FIELD; // remove size field
            // read out explicit OBU size
            num_lebs = parse_leb(ctx, obu_ptr, frame_size, &obu_size);
            if (!num_lebs) {
                return;
            }
            obu_ptr += num_lebs;
            frame_size -= num_lebs;
        } else {
            av_log(ctx, AV_LOG_ERROR, "Cannot handle AV1 OBUs without size fields\n");
            return;
        }

        if ((long) obu_size > frame_size) {
            av_log(ctx, AV_LOG_ERROR, "AV1 OBU size %d larger than remaining frame size %d\n", obu_size, frame_size);
            return;
        }

        if (obu_size > 0xfffffffd) {
            av_log(ctx, AV_LOG_ERROR, "AV1 OBU size 0x%x might overflow (attack?)\n", obu_size);
            return;
        }

        frame_size -= obu_size;

        if ((obu_type == AV1_OBU_TEMPORAL_DELIMITER) ||
            (obu_type == AV1_OBU_TILE_LIST) ||
            (obu_type == AV1_OBU_PADDING)) {
            // ignore and remove according to spec (note that OBU_PADDING is not
            // mentioned in spec, but it does not make sense to transmit it).
            obu_ptr += obu_size;
            // additional handling if the ignored OBU was the last one
            if (!frame_size) {
                // we're done, flush the last packet, set RTP marker bit
                last_packet_of_frame = 1;
                goto flush_last_packet;
            }
            continue;
        }

        /* if the last OBU had a temporal or spatial ID, they need to match to
         * current; otherwise start new packet */
        if ((last_obu_ext >= 0) && (curr_obu_ext != last_obu_ext)) {
            start_new_packet = 1;
        }

flush_last_packet:
        last_obu_ptr = curr_obu_ptr;
        last_elem_size = curr_elem_size;
        last_obu_hdr = curr_obu_hdr;
        last_obu_ext = curr_obu_ext;

        curr_obu_ptr = obu_ptr; // behind header
        curr_elem_size = obu_size + 1 + ((ext_byte >= 0) ? 1 : 0);
        curr_obu_hdr = obu_hdr;
        curr_obu_ext = ext_byte;

        obu_ptr += obu_size;

        if (last_obu_ptr) {
            unsigned int first_elem_with_size = last_elem_size + calc_leb_size(last_elem_size);
            // check if last packet fits completely and has reasonable space for
            // at least a fragment of the next
            if (!last_packet_of_frame && (first_elem_with_size + 10 < rem_pkt_size)) {
                num_lebs = write_leb(pkt_ptr, last_elem_size);
                pkt_ptr += num_lebs;
                rem_pkt_size -= num_lebs;
            } else {
                if ((num_obus >= 3) && (last_packet_of_frame || (first_elem_with_size <= rem_pkt_size))) {
                    // last fits with forced size, but nothing else
                    num_lebs = write_leb(pkt_ptr, last_elem_size);
                    pkt_ptr += num_lebs;
                    rem_pkt_size -= num_lebs;
                }
                // force new packet
                start_new_packet = 1;
            }

            // write header and optional extension byte (if not a continued fragment)
            if (last_obu_hdr >= 0) {
                *pkt_ptr++ = last_obu_hdr;
                last_elem_size--;
                rem_pkt_size--;
                if (last_obu_ext >= 0) {
                    *pkt_ptr++ = last_obu_ext;
                    last_elem_size--;
                    rem_pkt_size--;
                }
            }
            // copy payload
            memcpy(pkt_ptr, last_obu_ptr, last_elem_size);
            pkt_ptr += last_elem_size;
            rem_pkt_size -= last_elem_size;
            num_obus++;
        }

        if (start_new_packet || last_packet_of_frame) {
            if (num_obus < 4) {
                aggr_hdr |= num_obus << AV1S_AGGR_HDR_NUM_OBUS;
            }
            rtp_ctx->buf[0] = aggr_hdr;

#ifdef RTPENC_AV1_VERBOSE_TRACE
            av_log(ctx, AV_LOG_TRACE, "Sending NON-FRAG packet no %d, %ld/%d, %d OBUs (marker=%d)\n",
                   ((RTPMuxContext *) ctx->priv_data)->seq,
                   pkt_ptr - rtp_ctx->buf, rtp_ctx->max_payload_size, num_obus, last_packet_of_frame);
            av_hex_dump_log(ctx, AV_LOG_TRACE, rtp_ctx->buf, FFMIN(pkt_ptr - rtp_ctx->buf, 64));
            av_log(ctx, AV_LOG_TRACE, "... end at offset %lx:\n", FFMAX((pkt_ptr - rtp_ctx->buf) - 64, 0));
            av_hex_dump_log(ctx, AV_LOG_TRACE, rtp_ctx->buf + FFMAX((pkt_ptr - rtp_ctx->buf) - 64, 0), FFMIN(pkt_ptr - rtp_ctx->buf, 64));
#endif

            ff_rtp_send_data(ctx, rtp_ctx->buf, pkt_ptr - rtp_ctx->buf, last_packet_of_frame);

            rem_pkt_size = rtp_ctx->max_payload_size - 1;
            pkt_ptr = rtp_ctx->buf + 1;
            aggr_hdr = 0;
            num_obus = 0;
        }

        if (last_packet_of_frame) {
            break;
        }

        // check if element needs to be fragmented, otherwise we will deal with
        // it in the next iteration
        if ((curr_elem_size > rem_pkt_size) ||
            ((num_obus >= 3) && (curr_elem_size + calc_leb_size(curr_elem_size)) > rem_pkt_size)) {
            uint32_t frag_size = rem_pkt_size;

            // if there are going more than 3 OBU elements, we are obliged to
            // have the length field for the last
            if (num_obus >= 3) {
                // that's an upper limit of LEBs
                num_lebs = calc_leb_size(rem_pkt_size - 1);
                frag_size -= num_lebs;

                // write a fixed number of LEBs, in case the frag_size could
                // now be specified with one less byte
                write_leb_n(pkt_ptr, frag_size, num_lebs);
                pkt_ptr += num_lebs;
                rem_pkt_size -= num_lebs;
            }

            // write header and optional extension byte
            *pkt_ptr++ = curr_obu_hdr;
            curr_elem_size--;
            rem_pkt_size--;
            if (curr_obu_ext >= 0) {
                *pkt_ptr++ = curr_obu_ext;
                curr_elem_size--;
                rem_pkt_size--;
            }

            // disable header writing for final fragment
            curr_obu_hdr = -1;
            curr_obu_ext = -1;

            // send more full packet sized fragments
            do {
                // copy payload
                memcpy(pkt_ptr, curr_obu_ptr, rem_pkt_size);
                pkt_ptr += rem_pkt_size;
                curr_obu_ptr += rem_pkt_size;
                curr_elem_size -= rem_pkt_size;
                num_obus++;

                aggr_hdr |= AV1F_AGGR_HDR_LAST_FRAG;
                if (num_obus < 4) {
                    aggr_hdr |= num_obus << AV1S_AGGR_HDR_NUM_OBUS;
                }
                rtp_ctx->buf[0] = aggr_hdr;

#ifdef RTPENC_AV1_VERBOSE_TRACE
                av_log(ctx, AV_LOG_DEBUG, "Sending FRAG packet no %d, %ld/%d, %d OBUs\n",
                       ((RTPMuxContext *) ctx->priv_data)->seq,
                       pkt_ptr - rtp_ctx->buf, rtp_ctx->max_payload_size, num_obus);
                av_hex_dump_log(ctx, AV_LOG_TRACE, rtp_ctx->buf, FFMIN(pkt_ptr - rtp_ctx->buf, 64));
                av_log(ctx, AV_LOG_TRACE, "... end at offset %lx:\n", FFMAX((pkt_ptr - rtp_ctx->buf) - 64, 0));
                av_hex_dump_log(ctx, AV_LOG_TRACE, rtp_ctx->buf + FFMAX((pkt_ptr - rtp_ctx->buf) - 64, 0), FFMIN(pkt_ptr - rtp_ctx->buf, 64));
#endif

                ff_rtp_send_data(ctx, rtp_ctx->buf, pkt_ptr - rtp_ctx->buf, 0);
                rem_pkt_size = rtp_ctx->max_payload_size - 1;
                pkt_ptr = rtp_ctx->buf + 1;

                aggr_hdr = AV1F_AGGR_HDR_FRAG_CONT;
                num_obus = 0;
            } while (curr_elem_size > rem_pkt_size);
            start_new_packet = 0;
        }

        if (!frame_size) {
            // we're done, flush the last packet, set RTP marker bit
            last_packet_of_frame = 1;
            goto flush_last_packet;
        }
    }
}
