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
 * @file
 * @brief Realmedia RTSP protocol (RDT) support
 * @author Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 */

#include "avformat.h"
#include "libavutil/avstring.h"
#include "rtpdec.h"
#include "rdt.h"
#include "libavutil/base64.h"
#include "libavutil/md5.h"
#include "rm.h"
#include "internal.h"
#include "avio_internal.h"
#include "libavcodec/get_bits.h"

struct RDTDemuxContext {
    AVFormatContext *ic; /**< the containing (RTSP) demux context */
    /** Each RDT stream-set (represented by one RTSPStream) can contain
     * multiple streams (of the same content, but with possibly different
     * codecs/bitrates). Each such stream is represented by one AVStream
     * in the AVFormatContext, and this variable points to the offset in
     * that array such that the first is the first stream of this set. */
    AVStream **streams;
    int n_streams; /**< streams with identifical content in this set */
    void *dynamic_protocol_context;
    DynamicPayloadPacketHandlerProc parse_packet;
    uint32_t prev_timestamp;
    int prev_set_id, prev_stream_id;
};

RDTDemuxContext *
ff_rdt_parse_open(AVFormatContext *ic, int first_stream_of_set_idx,
                  void *priv_data, RTPDynamicProtocolHandler *handler)
{
    RDTDemuxContext *s = av_mallocz(sizeof(RDTDemuxContext));
    if (!s)
        return NULL;

    s->ic = ic;
    s->streams = &ic->streams[first_stream_of_set_idx];
    do {
        s->n_streams++;
    } while (first_stream_of_set_idx + s->n_streams < ic->nb_streams &&
             s->streams[s->n_streams]->id == s->streams[0]->id);
    s->prev_set_id    = -1;
    s->prev_stream_id = -1;
    s->prev_timestamp = -1;
    s->parse_packet = handler ? handler->parse_packet : NULL;
    s->dynamic_protocol_context = priv_data;

    return s;
}

void
ff_rdt_parse_close(RDTDemuxContext *s)
{
    av_free(s);
}

struct PayloadContext {
    AVFormatContext *rmctx;
    int nb_rmst;
    RMStream **rmst;
    uint8_t *mlti_data;
    unsigned int mlti_data_size;
    char buffer[RTP_MAX_PACKET_LENGTH + FF_INPUT_BUFFER_PADDING_SIZE];
    int audio_pkt_cnt; /**< remaining audio packets in rmdec */
};

void
ff_rdt_calc_response_and_checksum(char response[41], char chksum[9],
                                  const char *challenge)
{
    int ch_len = strlen (challenge), i;
    unsigned char zres[16],
        buf[64] = { 0xa1, 0xe9, 0x14, 0x9d, 0x0e, 0x6b, 0x3b, 0x59 };
#define XOR_TABLE_SIZE 37
    static const unsigned char xor_table[XOR_TABLE_SIZE] = {
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
    ff_data_to_hex(response, zres, 16, 1);

    /* add tail */
    strcpy (response + 32, "01d0a8e3");

    /* calculate checksum */
    for (i = 0; i < 8; i++)
        chksum[i] = response[i * 4];
    chksum[8] = 0;
}

static int
rdt_load_mdpr (PayloadContext *rdt, AVStream *st, int rule_nr)
{
    AVIOContext pb;
    int size;
    uint32_t tag;

    /**
     * Layout of the MLTI chunk:
     * 4: MLTI
     * 2: number of streams
     * Then for each stream ([number_of_streams] times):
     *     2: mdpr index
     * 2: number of mdpr chunks
     * Then for each mdpr chunk ([number_of_mdpr_chunks] times):
     *     4: size
     *     [size]: data
     * we skip MDPR chunks until we reach the one of the stream
     * we're interested in, and forward that ([size]+[data]) to
     * the RM demuxer to parse the stream-specific header data.
     */
    if (!rdt->mlti_data)
        return -1;
    ffio_init_context(&pb, rdt->mlti_data, rdt->mlti_data_size, 0,
                  NULL, NULL, NULL, NULL);
    tag = avio_rl32(&pb);
    if (tag == MKTAG('M', 'L', 'T', 'I')) {
        int num, chunk_nr;

        /* read index of MDPR chunk numbers */
        num = avio_rb16(&pb);
        if (rule_nr < 0 || rule_nr >= num)
            return -1;
        avio_skip(&pb, rule_nr * 2);
        chunk_nr = avio_rb16(&pb);
        avio_skip(&pb, (num - 1 - rule_nr) * 2);

        /* read MDPR chunks */
        num = avio_rb16(&pb);
        if (chunk_nr >= num)
            return -1;
        while (chunk_nr--)
            avio_skip(&pb, avio_rb32(&pb));
        size = avio_rb32(&pb);
    } else {
        size = rdt->mlti_data_size;
        avio_seek(&pb, 0, SEEK_SET);
    }
    if (ff_rm_read_mdpr_codecdata(rdt->rmctx, &pb, st, rdt->rmst[st->index], size, NULL) < 0)
        return -1;

    return 0;
}

/**
 * Actual data handling.
 */

int
ff_rdt_parse_header(const uint8_t *buf, int len,
                    int *pset_id, int *pseq_no, int *pstream_id,
                    int *pis_keyframe, uint32_t *ptimestamp)
{
    GetBitContext gb;
    int consumed = 0, set_id, seq_no, stream_id, is_keyframe,
        len_included, need_reliable;
    uint32_t timestamp;

    /* skip status packets */
    while (len >= 5 && buf[1] == 0xFF /* status packet */) {
        int pkt_len;

        if (!(buf[0] & 0x80))
            return -1; /* not followed by a data packet */

        pkt_len = AV_RB16(buf+3);
        buf += pkt_len;
        len -= pkt_len;
        consumed += pkt_len;
    }
    if (len < 16)
        return -1;
    /**
     * Layout of the header (in bits):
     * 1:  len_included
     *     Flag indicating whether this header includes a length field;
     *     this can be used to concatenate multiple RDT packets in a
     *     single UDP/TCP data frame and is used to precede RDT data
     *     by stream status packets
     * 1:  need_reliable
     *     Flag indicating whether this header includes a "reliable
     *     sequence number"; these are apparently sequence numbers of
     *     data packets alone. For data packets, this flag is always
     *     set, according to the Real documentation [1]
     * 5:  set_id
     *     ID of a set of streams of identical content, possibly with
     *     different codecs or bitrates
     * 1:  is_reliable
     *     Flag set for certain streams deemed less tolerable for packet
     *     loss
     * 16: seq_no
     *     Packet sequence number; if >=0xFF00, this is a non-data packet
     *     containing stream status info, the second byte indicates the
     *     type of status packet (see wireshark docs / source code [2])
     * if (len_included) {
     *     16: packet_len
     * } else {
     *     packet_len = remainder of UDP/TCP frame
     * }
     * 1:  is_back_to_back
     *     Back-to-Back flag; used for timing, set for one in every 10
     *     packets, according to the Real documentation [1]
     * 1:  is_slow_data
     *     Slow-data flag; currently unused, according to Real docs [1]
     * 5:  stream_id
     *     ID of the stream within this particular set of streams
     * 1:  is_no_keyframe
     *     Non-keyframe flag (unset if packet belongs to a keyframe)
     * 32: timestamp (PTS)
     * if (set_id == 0x1F) {
     *     16: set_id (extended set-of-streams ID; see set_id)
     * }
     * if (need_reliable) {
     *     16: reliable_seq_no
     *         Reliable sequence number (see need_reliable)
     * }
     * if (stream_id == 0x3F) {
     *     16: stream_id (extended stream ID; see stream_id)
     * }
     * [1] https://protocol.helixcommunity.org/files/2005/devdocs/RDT_Feature_Level_20.txt
     * [2] http://www.wireshark.org/docs/dfref/r/rdt.html and
     *     http://anonsvn.wireshark.org/viewvc/trunk/epan/dissectors/packet-rdt.c
     */
    init_get_bits(&gb, buf, len << 3);
    len_included  = get_bits1(&gb);
    need_reliable = get_bits1(&gb);
    set_id        = get_bits(&gb, 5);
    skip_bits(&gb, 1);
    seq_no        = get_bits(&gb, 16);
    if (len_included)
        skip_bits(&gb, 16);
    skip_bits(&gb, 2);
    stream_id     = get_bits(&gb, 5);
    is_keyframe   = !get_bits1(&gb);
    timestamp     = get_bits_long(&gb, 32);
    if (set_id == 0x1f)
        set_id    = get_bits(&gb, 16);
    if (need_reliable)
        skip_bits(&gb, 16);
    if (stream_id == 0x1f)
        stream_id = get_bits(&gb, 16);

    if (pset_id)      *pset_id      = set_id;
    if (pseq_no)      *pseq_no      = seq_no;
    if (pstream_id)   *pstream_id   = stream_id;
    if (pis_keyframe) *pis_keyframe = is_keyframe;
    if (ptimestamp)   *ptimestamp   = timestamp;

    return consumed + (get_bits_count(&gb) >> 3);
}

/**< return 0 on packet, no more left, 1 on packet, 1 on partial packet... */
static int
rdt_parse_packet (AVFormatContext *ctx, PayloadContext *rdt, AVStream *st,
                  AVPacket *pkt, uint32_t *timestamp,
                  const uint8_t *buf, int len, uint16_t rtp_seq, int flags)
{
    int seq = 1, res;
    AVIOContext pb;

    if (rdt->audio_pkt_cnt == 0) {
        int pos;

        ffio_init_context(&pb, buf, len, 0, NULL, NULL, NULL, NULL);
        flags = (flags & RTP_FLAG_KEY) ? 2 : 0;
        res = ff_rm_parse_packet (rdt->rmctx, &pb, st, rdt->rmst[st->index], len, pkt,
                                  &seq, flags, *timestamp);
        pos = avio_tell(&pb);
        if (res < 0)
            return res;
        if (res > 0) {
            if (st->codec->codec_id == AV_CODEC_ID_AAC) {
                memcpy (rdt->buffer, buf + pos, len - pos);
                rdt->rmctx->pb = avio_alloc_context (rdt->buffer, len - pos, 0,
                                                    NULL, NULL, NULL, NULL);
            }
            goto get_cache;
        }
    } else {
get_cache:
        rdt->audio_pkt_cnt =
            ff_rm_retrieve_cache (rdt->rmctx, rdt->rmctx->pb,
                                  st, rdt->rmst[st->index], pkt);
        if (rdt->audio_pkt_cnt == 0 &&
            st->codec->codec_id == AV_CODEC_ID_AAC)
            av_freep(&rdt->rmctx->pb);
    }
    pkt->stream_index = st->index;
    pkt->pts = *timestamp;

    return rdt->audio_pkt_cnt > 0;
}

int
ff_rdt_parse_packet(RDTDemuxContext *s, AVPacket *pkt,
                    uint8_t **bufptr, int len)
{
    uint8_t *buf = bufptr ? *bufptr : NULL;
    int seq_no, flags = 0, stream_id, set_id, is_keyframe;
    uint32_t timestamp;
    int rv= 0;

    if (!s->parse_packet)
        return -1;

    if (!buf && s->prev_stream_id != -1) {
        /* return the next packets, if any */
        timestamp= 0; ///< Should not be used if buf is NULL, but should be set to the timestamp of the packet returned....
        rv= s->parse_packet(s->ic, s->dynamic_protocol_context,
                            s->streams[s->prev_stream_id],
                            pkt, &timestamp, NULL, 0, 0, flags);
        return rv;
    }

    if (len < 12)
        return -1;
    rv = ff_rdt_parse_header(buf, len, &set_id, &seq_no, &stream_id, &is_keyframe, &timestamp);
    if (rv < 0)
        return rv;
    if (is_keyframe &&
        (set_id != s->prev_set_id || timestamp != s->prev_timestamp ||
         stream_id != s->prev_stream_id)) {
        flags |= RTP_FLAG_KEY;
        s->prev_set_id    = set_id;
        s->prev_timestamp = timestamp;
    }
    s->prev_stream_id = stream_id;
    buf += rv;
    len -= rv;

     if (s->prev_stream_id >= s->n_streams) {
         s->prev_stream_id = -1;
         return -1;
     }

    rv = s->parse_packet(s->ic, s->dynamic_protocol_context,
                         s->streams[s->prev_stream_id],
                         pkt, &timestamp, buf, len, 0, flags);

    return rv;
}

void
ff_rdt_subscribe_rule (char *cmd, int size,
                       int stream_nr, int rule_nr)
{
    av_strlcatf(cmd, size, "stream=%d;rule=%d,stream=%d;rule=%d",
                stream_nr, rule_nr * 2, stream_nr, rule_nr * 2 + 1);
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
rdt_parse_sdp_line (AVFormatContext *s, int st_index,
                    PayloadContext *rdt, const char *line)
{
    AVStream *stream = s->streams[st_index];
    const char *p = line;

    if (av_strstart(p, "OpaqueData:buffer;", &p)) {
        rdt->mlti_data = rdt_parse_b64buf(&rdt->mlti_data_size, p);
    } else if (av_strstart(p, "StartTime:integer;", &p))
        stream->first_dts = atoi(p);
    else if (av_strstart(p, "ASMRuleBook:string;", &p)) {
        int n, first = -1;

        for (n = 0; n < s->nb_streams; n++)
            if (s->streams[n]->id == stream->id) {
                int count = s->streams[n]->index + 1, err;
                if (first == -1) first = n;
                if (rdt->nb_rmst < count) {
                    if ((err = av_reallocp(&rdt->rmst,
                                           count * sizeof(*rdt->rmst))) < 0)
                        return err;
                    memset(rdt->rmst + rdt->nb_rmst, 0,
                           (count - rdt->nb_rmst) * sizeof(*rdt->rmst));
                    rdt->nb_rmst = count;
                }
                rdt->rmst[s->streams[n]->index] = ff_rm_alloc_rmstream();
                rdt_load_mdpr(rdt, s->streams[n], (n - first) * 2);
           }
    }

    return 0;
}

static void
real_parse_asm_rule(AVStream *st, const char *p, const char *end)
{
    do {
        /* can be either averagebandwidth= or AverageBandwidth= */
        if (sscanf(p, " %*1[Aa]verage%*1[Bb]andwidth=%d", &st->codec->bit_rate) == 1)
            break;
        if (!(p = strchr(p, ',')) || p > end)
            p = end;
        p++;
    } while (p < end);
}

static AVStream *
add_dstream(AVFormatContext *s, AVStream *orig_st)
{
    AVStream *st;

    if (!(st = avformat_new_stream(s, NULL)))
        return NULL;
    st->id = orig_st->id;
    st->codec->codec_type = orig_st->codec->codec_type;
    st->first_dts         = orig_st->first_dts;

    return st;
}

static void
real_parse_asm_rulebook(AVFormatContext *s, AVStream *orig_st,
                        const char *p)
{
    const char *end;
    int n_rules = 0, odd = 0;
    AVStream *st;

    /**
     * The ASMRuleBook contains a list of comma-separated strings per rule,
     * and each rule is separated by a ;. The last one also has a ; at the
     * end so we can use it as delimiter.
     * Every rule occurs twice, once for when the RTSP packet header marker
     * is set and once for if it isn't. We only read the first because we
     * don't care much (that's what the "odd" variable is for).
     * Each rule contains a set of one or more statements, optionally
     * preceded by a single condition. If there's a condition, the rule
     * starts with a '#'. Multiple conditions are merged between brackets,
     * so there are never multiple conditions spread out over separate
     * statements. Generally, these conditions are bitrate limits (min/max)
     * for multi-bitrate streams.
     */
    if (*p == '\"') p++;
    while (1) {
        if (!(end = strchr(p, ';')))
            break;
        if (!odd && end != p) {
            if (n_rules > 0)
                st = add_dstream(s, orig_st);
            else
                st = orig_st;
            if (!st)
                break;
            real_parse_asm_rule(st, p, end);
            n_rules++;
        }
        p = end + 1;
        odd ^= 1;
    }
}

void
ff_real_parse_sdp_a_line (AVFormatContext *s, int stream_index,
                          const char *line)
{
    const char *p = line;

    if (av_strstart(p, "ASMRuleBook:string;", &p))
        real_parse_asm_rulebook(s, s->streams[stream_index], p);
}

static PayloadContext *
rdt_new_context (void)
{
    PayloadContext *rdt = av_mallocz(sizeof(PayloadContext));

    int ret = avformat_open_input(&rdt->rmctx, "", &ff_rdt_demuxer, NULL);
    if (ret < 0) {
        av_free(rdt);
        return NULL;
    }

    return rdt;
}

static void
rdt_free_context (PayloadContext *rdt)
{
    int i;

    for (i = 0; i < rdt->nb_rmst; i++)
        if (rdt->rmst[i]) {
            ff_rm_free_rmstream(rdt->rmst[i]);
            av_freep(&rdt->rmst[i]);
        }
    if (rdt->rmctx)
        avformat_close_input(&rdt->rmctx);
    av_freep(&rdt->mlti_data);
    av_freep(&rdt->rmst);
    av_free(rdt);
}

#define RDT_HANDLER(n, s, t) \
static RTPDynamicProtocolHandler rdt_ ## n ## _handler = { \
    .enc_name         = s, \
    .codec_type       = t, \
    .codec_id         = AV_CODEC_ID_NONE, \
    .parse_sdp_a_line = rdt_parse_sdp_line, \
    .alloc            = rdt_new_context, \
    .free             = rdt_free_context, \
    .parse_packet     = rdt_parse_packet \
}

RDT_HANDLER(live_video, "x-pn-multirate-realvideo-live", AVMEDIA_TYPE_VIDEO);
RDT_HANDLER(live_audio, "x-pn-multirate-realaudio-live", AVMEDIA_TYPE_AUDIO);
RDT_HANDLER(video,      "x-pn-realvideo",                AVMEDIA_TYPE_VIDEO);
RDT_HANDLER(audio,      "x-pn-realaudio",                AVMEDIA_TYPE_AUDIO);

void av_register_rdt_dynamic_payload_handlers(void)
{
    ff_register_dynamic_payload_handler(&rdt_video_handler);
    ff_register_dynamic_payload_handler(&rdt_audio_handler);
    ff_register_dynamic_payload_handler(&rdt_live_video_handler);
    ff_register_dynamic_payload_handler(&rdt_live_audio_handler);
}
