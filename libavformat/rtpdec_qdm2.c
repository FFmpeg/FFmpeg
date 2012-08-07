/*
 * QDesign Music 2 (QDM2) payload for RTP
 * Copyright (c) 2010 Ronald S. Bultje
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
 * @brief RTP support for the QDM2 payload (todo: wiki)
 * @author Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 */

#include <string.h>
#include "libavutil/intreadwrite.h"
#include "libavcodec/avcodec.h"
#include "rtp.h"
#include "rtpdec.h"
#include "rtpdec_formats.h"

struct PayloadContext {
    /** values read from the config header, used as packet headers */
    //@{
    int block_type;            ///< superblock type, value 2 .. 8
    int block_size;            ///< from extradata, used as pkt length
    int subpkts_per_block;     ///< max. nr. of subpackets to add per output buffer
    //@}

    /** Temporary storage for superblock restoring, per packet ID (0x80 total) */
    //@{
    uint16_t len[0x80];        ///< how much the temporary buffer is filled
    uint8_t  buf[0x80][0x800]; ///< the temporary storage buffer

    unsigned int cache;        ///< number of data packets that we have cached right now
    unsigned int n_pkts;       ///< number of RTP packets received since last packet output / config
    uint32_t timestamp;        ///< timestamp of next-to-be-returned packet
    //@}
};

/**
 * Parse configuration (basically the codec-specific extradata) from
 * an RTP config subpacket (starts with 0xff).
 *
 * Layout of the config subpacket (in bytes):
 * 1: 0xFF          <- config ID
 * then an array {
 *     1: size      <- of the current item
 *     1: item type <- 0 .. 4
 *     size-2: data <- data depends on the item type
 * }
 *
 * Item 0 implies the end of the config subpacket, and has no data.
 * Item 1 implies a stream configuration without extradata.
 * Item 2 max. nr. of subpackets per superblock
 * Item 3 superblock type for the stream
 * Item 4 implies a stream configuration with extradata (size >= 0x1c).
 *
 * @return <0 on error, otherwise the number of bytes parsed from the
 *         input buffer.
 */
static int qdm2_parse_config(PayloadContext *qdm, AVStream *st,
                             const uint8_t *buf, const uint8_t *end)
{
    const uint8_t *p = buf;

    while (end - p >= 2) {
        unsigned int item_len = p[0], config_item = p[1];

        if (item_len < 2 || end - p < item_len || config_item > 4)
            return AVERROR_INVALIDDATA;

        switch (config_item) {
            case 0: /* end of config block */
                return p - buf + item_len;
            case 1: /* stream without extradata */
                /* FIXME: set default qdm->block_size */
                break;
            case 2: /**< subpackets per block */
                if (item_len < 3)
                    return AVERROR_INVALIDDATA;
                qdm->subpkts_per_block = p[2];
                break;
            case 3: /* superblock type */
                if (item_len < 4)
                    return AVERROR_INVALIDDATA;
                qdm->block_type = AV_RB16(p + 2);
                break;
            case 4: /* stream with extradata */
                if (item_len < 30)
                    return AVERROR_INVALIDDATA;
                av_freep(&st->codec->extradata);
                st->codec->extradata_size = 26 + item_len;
                if (!(st->codec->extradata = av_mallocz(st->codec->extradata_size + FF_INPUT_BUFFER_PADDING_SIZE))) {
                    st->codec->extradata_size = 0;
                    return AVERROR(ENOMEM);
                }
                AV_WB32(st->codec->extradata, 12);
                memcpy(st->codec->extradata + 4, "frma", 4);
                memcpy(st->codec->extradata + 8, "QDM2", 4);
                AV_WB32(st->codec->extradata + 12, 6 + item_len);
                memcpy(st->codec->extradata + 16, "QDCA", 4);
                memcpy(st->codec->extradata + 20, p + 2, item_len - 2);
                AV_WB32(st->codec->extradata + 18 + item_len, 8);
                AV_WB32(st->codec->extradata + 22 + item_len, 0);

                qdm->block_size = AV_RB32(p + 26);
                break;
        }

        p += item_len;
    }

    return AVERROR(EAGAIN); /* not enough data */
}

/**
 * Parse a single subpacket. We store this subpacket in an intermediate
 * buffer (position depends on the ID (byte[0]). When called, at least
 * 4 bytes are available for reading (see qdm2_parse_packet()).
 *
 * Layout of a single subpacket (RTP packets commonly contain multiple
 * such subpackets) - length in bytes:
 * 1:    ordering ID        <- 0 .. 0x7F
 * 1:    subpacket type     <- 0 .. 0x7F; value & 0x80 means subpacket length = 2 bytes, else 1 byte
 * 1/2:  subpacket length   <- length of the data following the flags/length fields
 * if (subpacket type & 0x7F) == 0x7F
 *   1:  subpacket type, higher bits
 * size: subpacket data
 *
 * The subpackets come in randomly, and should be encapsulated into 1
 * or more superblocks (containing qdm->subpkts_per_block subpackets
 * each) per RTP packet, in order of ascending "ordering ID", see
 * qdm2_restore_block().
 *
 * @return <0 on error, otherwise the number of bytes parsed from the
 *         input buffer.
 */
static int qdm2_parse_subpacket(PayloadContext *qdm, AVStream *st,
                                const uint8_t *buf, const uint8_t *end)
{
    const uint8_t *p = buf;
    unsigned int id, len, type, to_copy;

    /* parse header so we know the size of the header/data */
    id       = *p++;
    type     = *p++;
    if (type & 0x80) {
        len   = AV_RB16(p);
        p    += 2;
        type &= 0x7F;
    } else
        len = *p++;

    if (end - p < len + (type == 0x7F) || id >= 0x80)
        return AVERROR_INVALIDDATA;
    if (type == 0x7F)
        type |= *p++ << 8;

    /* copy data into a temporary buffer */
    to_copy = FFMIN(len + (p - &buf[1]), 0x800 - qdm->len[id]);
    memcpy(&qdm->buf[id][qdm->len[id]], buf + 1, to_copy);
    qdm->len[id] += to_copy;

    return p + len - buf;
}

/**
 * Add a superblock header around a set of subpackets.
 *
 * @return <0 on error, else 0.
 */
static int qdm2_restore_block(PayloadContext *qdm, AVStream *st, AVPacket *pkt)
{
    int to_copy, n, res, include_csum;
    uint8_t *p, *csum_pos = NULL;

    /* create packet to hold subpkts into a superblock */
    assert(qdm->cache > 0);
    for (n = 0; n < 0x80; n++)
        if (qdm->len[n] > 0)
            break;
    assert(n < 0x80);

    if ((res = av_new_packet(pkt, qdm->block_size)) < 0)
        return res;
    memset(pkt->data, 0, pkt->size);
    pkt->stream_index  = st->index;
    p                  = pkt->data;

    /* superblock header */
    if (qdm->len[n] > 0xff) {
        *p++ = qdm->block_type | 0x80;
        AV_WB16(p, qdm->len[n]);
        p   += 2;
    } else {
        *p++ = qdm->block_type;
        *p++ = qdm->len[n];
    }
    if ((include_csum = (qdm->block_type == 2 || qdm->block_type == 4))) {
        csum_pos = p;
        p       += 2;
    }

    /* subpacket data */
    to_copy = FFMIN(qdm->len[n], pkt->size - (p - pkt->data));
    memcpy(p, qdm->buf[n], to_copy);
    qdm->len[n] = 0;

    /* checksum header */
    if (include_csum) {
        unsigned int total = 0;
        uint8_t *q;

        for (q = pkt->data; q < &pkt->data[qdm->block_size]; q++)
            total += *q;
        AV_WB16(csum_pos, (uint16_t) total);
    }

    return 0;
}

/** return 0 on packet, no more left, 1 on packet, -1 on partial packet... */
static int qdm2_parse_packet(AVFormatContext *s, PayloadContext *qdm,
                             AVStream *st, AVPacket *pkt,
                             uint32_t *timestamp,
                             const uint8_t *buf, int len, int flags)
{
    int res = AVERROR_INVALIDDATA, n;
    const uint8_t *end = buf + len, *p = buf;

    if (len > 0) {
        if (len < 2)
            return AVERROR_INVALIDDATA;

        /* configuration block */
        if (*p == 0xff) {
            if (qdm->n_pkts > 0) {
                av_log(s, AV_LOG_WARNING,
                       "Out of sequence config - dropping queue\n");
                qdm->n_pkts = 0;
                memset(qdm->len, 0, sizeof(qdm->len));
            }

            if ((res = qdm2_parse_config(qdm, st, ++p, end)) < 0)
                return res;
            p += res;

            /* We set codec_id to AV_CODEC_ID_NONE initially to
             * delay decoder initialization since extradata is
             * carried within the RTP stream, not SDP. Here,
             * by setting codec_id to AV_CODEC_ID_QDM2, we are signalling
             * to the decoder that it is OK to initialize. */
            st->codec->codec_id = AV_CODEC_ID_QDM2;
        }
        if (st->codec->codec_id == AV_CODEC_ID_NONE)
            return AVERROR(EAGAIN);

        /* subpackets */
        while (end - p >= 4) {
            if ((res = qdm2_parse_subpacket(qdm, st, p, end)) < 0)
                return res;
            p += res;
        }

        qdm->timestamp = *timestamp;
        if (++qdm->n_pkts < qdm->subpkts_per_block)
            return AVERROR(EAGAIN);
        qdm->cache = 0;
        for (n = 0; n < 0x80; n++)
            if (qdm->len[n] > 0)
                qdm->cache++;
    }

    /* output the subpackets into freshly created superblock structures */
    if (!qdm->cache || (res = qdm2_restore_block(qdm, st, pkt)) < 0)
        return res;
    if (--qdm->cache == 0)
        qdm->n_pkts = 0;

    *timestamp = qdm->timestamp;
    qdm->timestamp = RTP_NOTS_VALUE;

    return (qdm->cache > 0) ? 1 : 0;
}

static PayloadContext *qdm2_extradata_new(void)
{
    return av_mallocz(sizeof(PayloadContext));
}

static void qdm2_extradata_free(PayloadContext *qdm)
{
    av_free(qdm);
}

RTPDynamicProtocolHandler ff_qdm2_dynamic_handler = {
    .enc_name         = "X-QDM",
    .codec_type       = AVMEDIA_TYPE_AUDIO,
    .codec_id         = AV_CODEC_ID_NONE,
    .alloc            = qdm2_extradata_new,
    .free             = qdm2_extradata_free,
    .parse_packet     = qdm2_parse_packet,
};
