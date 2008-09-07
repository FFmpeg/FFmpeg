/*
 * Realmedia RTSP protocol (RDT) support.
 * Copyright (c) 2007 Ronald S. Bultje
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
 * @file rdt.c
 * @brief Realmedia RTSP protocol (RDT) support
 * @author Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 */

#include "avformat.h"
#include "libavutil/avstring.h"
#include "rtp_internal.h"
#include "rdt.h"
#include "libavutil/base64.h"
#include "libavutil/md5.h"
#include "rm.h"
#include "internal.h"

typedef struct rdt_data {
    AVFormatContext *rmctx;
    uint8_t *mlti_data;
    unsigned int mlti_data_size;
    uint32_t prev_sn, prev_ts;
    char buffer[RTP_MAX_PACKET_LENGTH + FF_INPUT_BUFFER_PADDING_SIZE];
} rdt_data;

void
ff_rdt_calc_response_and_checksum(char response[41], char chksum[9],
                                  const char *challenge)
{
    int ch_len = strlen (challenge), i;
    unsigned char zres[16],
        buf[64] = { 0xa1, 0xe9, 0x14, 0x9d, 0x0e, 0x6b, 0x3b, 0x59 };
#define XOR_TABLE_SIZE 37
    const unsigned char xor_table[XOR_TABLE_SIZE] = {
        0x05, 0x18, 0x74, 0xd0, 0x0d, 0x09, 0x02, 0x53,
        0xc0, 0x01, 0x05, 0x05, 0x67, 0x03, 0x19, 0x70,
        0x08, 0x27, 0x66, 0x10, 0x10, 0x72, 0x08, 0x09,
        0x63, 0x11, 0x03, 0x71, 0x08, 0x08, 0x70, 0x02,
        0x10, 0x57, 0x05, 0x18, 0x54 };

    /* some (length) checks */
    if (ch_len == 40) /* what a hack... */
        ch_len = 32;
    else if (ch_len > 56)
        ch_len = 56;
    memcpy(buf + 8, challenge, ch_len);

    /* xor challenge bytewise with xor_table */
    for (i = 0; i < XOR_TABLE_SIZE; i++)
        buf[8 + i] ^= xor_table[i];

    av_md5_sum(zres, buf, 64);
    ff_data_to_hex(response, zres, 16);
    for (i=0;i<32;i++) response[i] = tolower(response[i]);

    /* add tail */
    strcpy (response + 32, "01d0a8e3");

    /* calculate checksum */
    for (i = 0; i < 8; i++)
        chksum[i] = response[i * 4];
    chksum[8] = 0;
}

static int
rdt_load_mdpr (rdt_data *rdt, AVStream *st, int rule_nr)
{
    ByteIOContext *pb;
    int size;
    uint32_t tag;

    /**
     * Layout of the MLTI chunk:
     * 4:MLTI
     * 2:<number of streams>
     * Then for each stream ([number_of_streams] times):
     *     2:<mdpr index>
     * 2:<number of mdpr chunks>
     * Then for each mdpr chunk ([number_of_mdpr_chunks] times):
     *     4:<size>
     *     [size]:<data>
     * we skip MDPR chunks until we reach the one of the stream
     * we're interested in, and forward that ([size]+[data]) to
     * the RM demuxer to parse the stream-specific header data.
     */
    if (!rdt->mlti_data)
        return -1;
    url_open_buf(&pb, rdt->mlti_data, rdt->mlti_data_size, URL_RDONLY);
    tag = get_le32(pb);
    if (tag == MKTAG('M', 'L', 'T', 'I')) {
        int num, chunk_nr;

        /* read index of MDPR chunk numbers */
        num = get_be16(pb);
        if (rule_nr < 0 || rule_nr >= num)
            return -1;
        url_fskip(pb, rule_nr * 2);
        chunk_nr = get_be16(pb);
        url_fskip(pb, (num - 1 - rule_nr) * 2);

        /* read MDPR chunks */
        num = get_be16(pb);
        if (chunk_nr >= num)
            return -1;
        while (chunk_nr--)
            url_fskip(pb, get_be32(pb));
        size = get_be32(pb);
    } else {
        size = rdt->mlti_data_size;
        url_fseek(pb, 0, SEEK_SET);
    }
    rdt->rmctx->pb = pb;
    if (ff_rm_read_mdpr_codecdata(rdt->rmctx, st, size) < 0)
        return -1;

    url_close_buf(pb);
    return 0;
}

/**
 * Actual data handling.
 */

static int rdt_parse_header(struct RTPDemuxContext *s, const uint8_t *buf,
                            int len, int *seq, uint32_t *timestamp, int *flags)
{
    rdt_data *rdt = s->dynamic_protocol_context;
    int consumed = 0, sn;

    if (buf[0] < 0x40 || buf[0] > 0x42) {
        buf += 9;
        len -= 9;
        consumed += 9;
    }
    sn = (buf[0]>>1) & 0x1f;
    *seq = AV_RB16(buf+1);
    *timestamp = AV_RB32(buf+4);
    if (!(buf[3] & 1) && (sn != rdt->prev_sn || *timestamp != rdt->prev_ts)) {
        *flags |= PKT_FLAG_KEY;
        rdt->prev_sn = sn;
        rdt->prev_ts = *timestamp;
    }

    return consumed + 10;
}

/**< return 0 on packet, no more left, 1 on packet, 1 on partial packet... */
static int
rdt_parse_packet (RTPDemuxContext *s, AVPacket *pkt, uint32_t *timestamp,
                  const uint8_t *buf, int len, int flags)
{
    rdt_data *rdt = s->dynamic_protocol_context;
    int seq = 1, res;
    ByteIOContext *pb = rdt->rmctx->pb;
    RMContext *rm = rdt->rmctx->priv_data;
    AVStream *st = s->st;

    if (rm->audio_pkt_cnt == 0) {
        int pos;

        url_open_buf (&pb, buf, len, URL_RDONLY);
        flags = (flags & PKT_FLAG_KEY) ? 2 : 0;
        rdt->rmctx->pb = pb;
        res = ff_rm_parse_packet (rdt->rmctx, st, len, pkt,
                                  &seq, &flags, timestamp);
        pos = url_ftell(pb);
        url_close_buf (pb);
        if (res < 0)
            return res;
        if (rm->audio_pkt_cnt > 0 &&
            st->codec->codec_id == CODEC_ID_AAC) {
            memcpy (rdt->buffer, buf + pos, len - pos);
            url_open_buf (&pb, rdt->buffer, len - pos, URL_RDONLY);
            rdt->rmctx->pb = pb;
        }
    } else {
        ff_rm_retrieve_cache (rdt->rmctx, st, pkt);
        if (rm->audio_pkt_cnt == 0 &&
            st->codec->codec_id == CODEC_ID_AAC)
            url_close_buf (pb);
    }
    pkt->stream_index = st->index;
    pkt->pts = *timestamp;

    return rm->audio_pkt_cnt > 0;
}

int
ff_rdt_parse_packet(RTPDemuxContext *s, AVPacket *pkt,
                    const uint8_t *buf, int len)
{
    int seq, flags = 0;
    uint32_t timestamp;
    int rv= 0;

    if (!buf) {
        /* return the next packets, if any */
        timestamp= 0; ///< Should not be used if buf is NULL, but should be set to the timestamp of the packet returned....
        rv= rdt_parse_packet(s, pkt, &timestamp, NULL, 0, flags);
        return rv;
    }

    if (len < 12)
        return -1;
    rv = rdt_parse_header(s, buf, len, &seq, &timestamp, &flags);
    if (rv < 0)
        return rv;
    buf += rv;
    len -= rv;
    s->seq = seq;

    rv = rdt_parse_packet(s, pkt, &timestamp, buf, len, flags);

    return rv;
}

void
ff_rdt_subscribe_rule (RTPDemuxContext *s, char *cmd, int size,
                       int stream_nr, int rule_nr)
{
    rdt_data *rdt = s->dynamic_protocol_context;

    av_strlcatf(cmd, size, "stream=%d;rule=%d,stream=%d;rule=%d",
                stream_nr, rule_nr, stream_nr, rule_nr + 1);

    rdt_load_mdpr(rdt, s->st, 0);
}

static unsigned char *
rdt_parse_b64buf (unsigned int *target_len, const char *p)
{
    unsigned char *target;
    int len = strlen(p);
    if (*p == '\"') {
        p++;
        len -= 2; /* skip embracing " at start/end */
    }
    *target_len = len * 3 / 4;
    target = av_mallocz(*target_len + FF_INPUT_BUFFER_PADDING_SIZE);
    av_base64_decode(target, p, *target_len);
    return target;
}

static int
rdt_parse_sdp_line (AVStream *stream, void *d, const char *line)
{
    rdt_data *rdt = d;
    const char *p = line;

    if (av_strstart(p, "OpaqueData:buffer;", &p)) {
        rdt->mlti_data = rdt_parse_b64buf(&rdt->mlti_data_size, p);
    } else if (av_strstart(p, "StartTime:integer;", &p))
        stream->first_dts = atoi(p);

    return 0;
}

static void *
rdt_new_extradata (void)
{
    rdt_data *rdt = av_mallocz(sizeof(rdt_data));

    av_open_input_stream(&rdt->rmctx, NULL, "", &rdt_demuxer, NULL);
    rdt->prev_ts = -1;
    rdt->prev_sn = -1;

    return rdt;
}

static void
rdt_free_extradata (void *d)
{
    rdt_data *rdt = d;

    if (rdt->rmctx)
        av_close_input_stream(rdt->rmctx);
    av_freep(&rdt->mlti_data);
    av_free(rdt);
}

#define RDT_HANDLER(n, s, t) \
static RTPDynamicProtocolHandler ff_rdt_ ## n ## _handler = { \
    s, \
    t, \
    CODEC_ID_NONE, \
    rdt_parse_sdp_line, \
    rdt_new_extradata, \
    rdt_free_extradata \
};

RDT_HANDLER(live_video, "x-pn-multirate-realvideo-live", CODEC_TYPE_VIDEO);
RDT_HANDLER(live_audio, "x-pn-multirate-realaudio-live", CODEC_TYPE_AUDIO);
RDT_HANDLER(video,      "x-pn-realvideo",                CODEC_TYPE_VIDEO);
RDT_HANDLER(audio,      "x-pn-realaudio",                CODEC_TYPE_AUDIO);

void av_register_rdt_dynamic_payload_handlers(void)
{
    ff_register_dynamic_payload_handler(&ff_rdt_video_handler);
    ff_register_dynamic_payload_handler(&ff_rdt_audio_handler);
    ff_register_dynamic_payload_handler(&ff_rdt_live_video_handler);
    ff_register_dynamic_payload_handler(&ff_rdt_live_audio_handler);
}
