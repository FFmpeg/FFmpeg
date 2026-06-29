/*
 * Copyright (c) 2026 Torbjörn Einarsson
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

/*
 * Regression test for ff_rtp_parse_packet() and the RTP reordering queue,
 * exercised through the real MPEG-2 TS depacketizer (rtpdec_mpegts.c) with
 * realistic full-size RTP packets (12-byte RTP header + 7 * 188-byte TS
 * packets, i.e. the common 1328-byte MPEG-TS-over-RTP packet).
 *
 * The MPEG-TS depacketizer consumes an RTP payload only up to the point where
 * a payload unit start indicator (PUSI) closes the current PES. If that PUSI
 * sits on a TS packet other than the first in the RTP payload, the depacketizer
 * returns the finished PES, buffers the rest of the payload internally and
 * returns 1, expecting the caller to fetch the rest with a follow-up
 * buf == NULL call. A plain continuation RTP payload (no PUSI, the common case
 * in a video stream) completes no PES and makes the depacketizer return < 0.
 *
 * ff_rtp_parse_packet() routes the follow-up buf == NULL call back into the
 * depacketizer (to drain its buffer) only while s->prev_ret > 0. When a packet
 * is delivered from the "while (rv < 0 && has_next_packet(s))" reordering loop,
 * prev_ret used not to be updated, so after such a delivery the buffered tail
 * (the TS packets from the mid-payload PUSI onwards) was never drained and got
 * dropped.
 *
 * This needs nothing more exotic than a single reordered pair of packets: a
 * continuation packet (returns < 0) parsed while a mid-PUSI packet (returns 1)
 * is waiting in the queue is enough to enter that loop -- which is why the loss
 * was observed very frequently, not only on heavy reordering.
 *
 * The test feeds the exact same packets twice -- once in order (the reference;
 * the in-order path never enters the buggy loop) and once with one adjacent
 * pair swapped -- and checks that both deliver identical PES data. No packet is
 * ever lost, so a correct depacketizer must recover everything.
 */

#include <stdio.h>
#include <string.h>

#include "libavutil/mem.h"
#include "libavutil/intreadwrite.h"
#include "libavformat/avformat.h"
#include "libavformat/rtpdec.h"
#include "libavformat/rtpdec_formats.h"

#define TS_SIZE          188
#define TS_PAYLOAD       184            /* TS_SIZE - 4-byte TS header           */
#define TS_PER_RTP       7              /* 12 + 7*188 = 1328-byte RTP packets   */
#define RTP_HEADER_SIZE  12
#define RTP_PAYLOAD      (TS_PER_RTP * TS_SIZE)
#define MP2T_PT          33             /* static RTP payload type for MPEG-2 TS*/
#define QUEUE_SIZE       2              /* reordering enabled, small jitter buf */

#define ES_PID           0x0100
#define PES_STREAM_ID    0xe0           /* a video stream id                    */
#define PES_HDR_SIZE     9              /* 00 00 01 id len(2) flags(2) hdrlen    */

/* PES payload sizes that map onto a whole number of TS packets:
 * first packet carries PES_HDR_SIZE + (TS_PAYLOAD - PES_HDR_SIZE) ES bytes, the
 * rest carry TS_PAYLOAD each. 17 TS packets -> 175 + 16*184 ES bytes. */
#define PES_TS_PACKETS   17
#define PES_ES_LEN       ((TS_PAYLOAD - PES_HDR_SIZE) + (PES_TS_PACKETS - 1) * TS_PAYLOAD)

/* PES boundary layout (one PES per access unit), 35 TS packets = 5 RTP packets:
 *   PES_0 : TS 0..16   (PUSI on TS 0)
 *   PES_1 : TS 17..33  (PUSI on TS 17, mid RTP packet 2)
 *   PES_2 : TS 34      (PUSI on TS 34, closes PES_1; PES_2 left pending)
 * => RTP 1 (TS 7..13) is pure continuation, RTP 2 (TS 14..20) carries the
 *    mid-payload PUSI. Swapping their arrival order triggers the bug. */
#define NB_TS_PACKETS    35
#define NB_RTP_PACKETS   (NB_TS_PACKETS / TS_PER_RTP)   /* 5 */

/* Start sequence numbers away from 0: ff_rtp_parse_packet() treats s->seq == 0
 * as "no packet seen yet" and bypasses the reordering queue for it. */
#define SEQ_BASE         100

typedef struct Output {
    int       sizes[64];
    int       nb;
    uint8_t   bytes[1 << 16];
    int       total;
} Output;

/* ------------------------------------------------------------------ */
/* MPEG-TS stream construction                                        */
/* ------------------------------------------------------------------ */

static void put_ts_packet(uint8_t *dst, int pusi, int cc,
                          const uint8_t *payload)
{
    dst[0] = 0x47;                                   /* sync byte            */
    dst[1] = (pusi ? 0x40 : 0x00) | ((ES_PID >> 8) & 0x1f);
    dst[2] = ES_PID & 0xff;
    dst[3] = 0x10 | (cc & 0x0f);                     /* payload only, no AF  */
    memcpy(dst + 4, payload, TS_PAYLOAD);
}

/* Append one PES packet (header + es_len ES bytes) as TS packets. */
static void add_pes(uint8_t *ts, int *off, int *cc, int *es_counter, int es_len)
{
    static const uint8_t pes_hdr[PES_HDR_SIZE] = {
        0x00, 0x00, 0x01, PES_STREAM_ID,
        0x00, 0x00,             /* PES_packet_length 0 == unbounded           */
        0x80, 0x00, 0x00,       /* '10' marker, no PTS/DTS, header_data_len 0 */
    };
    int es_done = 0, first = 1;

    while (es_done < es_len) {
        uint8_t payload[TS_PAYLOAD];
        int p = 0, i, chunk;

        if (first) {
            memcpy(payload, pes_hdr, PES_HDR_SIZE);
            p = PES_HDR_SIZE;
        }
        chunk = TS_PAYLOAD - p;
        for (i = 0; i < chunk; i++)
            payload[p + i] = (uint8_t)((*es_counter)++ & 0xff);

        put_ts_packet(ts + *off, first, *cc, payload);
        *off += TS_SIZE;
        *cc   = (*cc + 1) & 0x0f;
        es_done += chunk;
        first = 0;
    }
}

static void build_ts_stream(uint8_t *ts)
{
    int off = 0, cc = 0, es_counter = 0;
    add_pes(ts, &off, &cc, &es_counter, PES_ES_LEN);   /* PES_0: TS 0..16  */
    add_pes(ts, &off, &cc, &es_counter, PES_ES_LEN);   /* PES_1: TS 17..33 */
    add_pes(ts, &off, &cc, &es_counter, TS_PAYLOAD - PES_HDR_SIZE); /* PES_2: TS 34 */
}

/* ------------------------------------------------------------------ */
/* RTP feeding                                                        */
/* ------------------------------------------------------------------ */

static void collect(AVPacket *pkt, Output *out)
{
    if (pkt->size > 0 && out->nb < FF_ARRAY_ELEMS(out->sizes) &&
        out->total + pkt->size <= (int)sizeof(out->bytes)) {
        out->sizes[out->nb++] = pkt->size;
        memcpy(out->bytes + out->total, pkt->data, pkt->size);
        out->total += pkt->size;
    }
    av_packet_unref(pkt);
}

/* Feed one freshly received RTP packet (carrying the block'th group of
 * TS_PER_RTP TS packets, with RTP sequence number SEQ_BASE + block) and collect
 * everything handed back, exactly as rtsp.c consumes ff_rtp_parse_packet():
 * a return value of 1 means "call again with buf == NULL". */
static void feed(RTPDemuxContext *s, AVPacket *pkt, const uint8_t *ts_stream,
                 int block, Output *out)
{
    int len = RTP_HEADER_SIZE + RTP_PAYLOAD, ret;
    uint8_t *buf = av_malloc(len);
    uint8_t *bufptr = buf;

    if (!buf)
        return;
    buf[0] = RTP_VERSION << 6;                 /* V=2, no padding/ext/CSRC    */
    buf[1] = MP2T_PT;                           /* no marker bit               */
    AV_WB16(buf + 2, SEQ_BASE + block);
    AV_WB32(buf + 4, 0);                        /* timestamp (unused)          */
    AV_WB32(buf + 8, 0x0badf00d);               /* ssrc                        */
    memcpy(buf + RTP_HEADER_SIZE, ts_stream + block * RTP_PAYLOAD, RTP_PAYLOAD);

    ret = ff_rtp_parse_packet(s, pkt, &bufptr, len);
    while (ret == 1) {
        collect(pkt, out);
        ret = ff_rtp_parse_packet(s, pkt, NULL, 0);
    }
    if (ret == 0)
        collect(pkt, out);

    if (bufptr)                                 /* not taken by the queue      */
        av_free(bufptr);
}

static int run(const uint8_t *ts_stream, const int *order, Output *out)
{
    AVFormatContext *fmt = avformat_alloc_context();
    RTPDemuxContext *s;
    PayloadContext *pc;
    AVPacket *pkt;
    int i, ret;

    memset(out, 0, sizeof(*out));
    if (!fmt)
        return AVERROR(ENOMEM);

    s = ff_rtp_parse_open(fmt, NULL, MP2T_PT, QUEUE_SIZE);
    pc = av_mallocz(ff_mpegts_dynamic_handler.priv_data_size);
    pkt = av_packet_alloc();
    if (!s || !pc || !pkt) {
        av_packet_free(&pkt);
        av_free(pc);
        if (s)
            ff_rtp_parse_close(s);
        avformat_free_context(fmt);
        return AVERROR(ENOMEM);
    }
    ret = ff_mpegts_dynamic_handler.init(fmt, -1, pc);
    if (ret < 0) {
        av_packet_free(&pkt);
        ff_mpegts_dynamic_handler.close(pc);
        av_free(pc);
        ff_rtp_parse_close(s);
        avformat_free_context(fmt);
        return ret;
    }
    ff_rtp_parse_set_dynamic_protocol(s, pc, &ff_mpegts_dynamic_handler);

    for (i = 0; i < NB_RTP_PACKETS; i++)
        feed(s, pkt, ts_stream, order[i], out);

    av_packet_free(&pkt);
    ff_mpegts_dynamic_handler.close(pc);
    av_free(pc);
    ff_rtp_parse_close(s);
    avformat_free_context(fmt);
    return 0;
}

static void dump(const char *label, const Output *o)
{
    int i;
    fprintf(stderr, "  %-9s %d packet(s), %d bytes, sizes:", label, o->nb, o->total);
    for (i = 0; i < o->nb; i++)
        fprintf(stderr, " %d", o->sizes[i]);
    fprintf(stderr, "\n");
}

int main(void)
{
    static uint8_t ts_stream[NB_TS_PACKETS * TS_SIZE];
    /* Block indices in arrival order; reordered swaps the continuation packet
     * (block 1, TS 7..13) and the mid-PUSI packet (block 2, TS 14..20). No
     * packet is dropped in either case. */
    static const int in_order[NB_RTP_PACKETS]  = { 0, 1, 2, 3, 4 };
    static const int reordered[NB_RTP_PACKETS] = { 0, 2, 1, 3, 4 };
    static Output ref, got;

    build_ts_stream(ts_stream);

    if (run(ts_stream, in_order,  &ref) < 0 ||
        run(ts_stream, reordered, &got) < 0) {
        fprintf(stderr, "test setup failed\n");
        return 2;
    }

    if (ref.nb != got.nb || ref.total != got.total ||
        memcmp(ref.sizes, got.sizes, ref.nb * sizeof(ref.sizes[0])) ||
        memcmp(ref.bytes, got.bytes, ref.total)) {
        fprintf(stderr,
                "FAIL: reordering an RTP packet with a mid-payload PUSI dropped "
                "MPEG-TS data.\n");
        dump("in-order:",  &ref);
        dump("reordered:", &got);
        return 1;
    }

    printf("OK: %d PES packets (%d bytes) recovered identically with and "
           "without reordering\n", ref.nb, ref.total);
    return 0;
}
