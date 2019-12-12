/*
 * Pro-MPEG Code of Practice #3 Release 2 FEC
 * Copyright (c) 2016 Mobibase, France (http://www.mobibase.com)
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
 * Pro-MPEG Code of Practice #3 Release 2 FEC protocol
 * @author Vlad Tarca <vlad.tarca@gmail.com>
 */

/*
 * Reminder:

 [RFC 2733] FEC Packet Structure

   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                         RTP Header                            |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                         FEC Header                            |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                         FEC Payload                           |
   |                                                               |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+


 [RFC 3550] RTP header

    0                   1                   2                   3
    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |V=2|P|X|  CC   |M|     PT      |       sequence number         |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                           timestamp                           |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |           synchronization source (SSRC) identifier            |
   +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
   |            contributing source (CSRC) identifiers             |
   |                             ....                              |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

 [RFC 3550] RTP header extension (after CSRC)

    0                   1                   2                   3
    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |      defined by profile       |           length              |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                        header extension                       |
   |                             ....                              |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

 [Pro-MPEG COP3] FEC Header

   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |      SNBase low bits          |        length recovery        |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |E| PT recovery |                 mask                          |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                          TS recovery                          |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |X|D|type |index|    offset     |      NA       |SNBase ext bits|
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

 */

#include "libavutil/avstring.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/random_seed.h"
#include "avformat.h"
#include "config.h"
#include "url.h"

#define PROMPEG_RTP_PT 0x60
#define PROMPEG_FEC_COL 0x0
#define PROMPEG_FEC_ROW 0x1

typedef struct PrompegFec {
    uint16_t sn;
    uint32_t ts;
    uint8_t *bitstring;
} PrompegFec;

typedef struct PrompegContext {
    const AVClass *class;
    URLContext *fec_col_hd, *fec_row_hd;
    PrompegFec **fec_arr, **fec_col_tmp, **fec_col, *fec_row;
    int ttl;
    uint8_t l, d;
    uint8_t *rtp_buf;
    uint16_t rtp_col_sn, rtp_row_sn;
    uint16_t length_recovery;
    int packet_size;
    int packet_idx, packet_idx_max;
    int fec_arr_len;
    int bitstring_size;
    int rtp_buf_size;
    int init;
    int first;
} PrompegContext;

#define OFFSET(x) offsetof(PrompegContext, x)
#define E AV_OPT_FLAG_ENCODING_PARAM

static const AVOption options[] = {
    { "ttl",   "Time to live (in milliseconds, multicast only)", OFFSET(ttl), AV_OPT_TYPE_INT, { .i64 = -1 }, -1, INT_MAX, .flags = E },
    { "l", "FEC L", OFFSET(l), AV_OPT_TYPE_INT, { .i64 =  5 }, 4, 20, .flags = E },
    { "d", "FEC D", OFFSET(d), AV_OPT_TYPE_INT, { .i64 =  5 }, 4, 20, .flags = E },
    { NULL }
};

static const AVClass prompeg_class = {
    .class_name = "prompeg",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static void xor_fast(const uint8_t *in1, const uint8_t *in2, uint8_t *out, int size) {
    int i, n, s;

#if HAVE_FAST_64BIT
    uint64_t v1, v2;

    n = size / sizeof (uint64_t);
    s = n * sizeof (uint64_t);

    for (i = 0; i < n; i++) {
        v1 = AV_RN64A(in1);
        v2 = AV_RN64A(in2);
        AV_WN64A(out, v1 ^ v2);
        in1 += 8;
        in2 += 8;
        out += 8;
    }
#else
    uint32_t v1, v2;

    n = size / sizeof (uint32_t);
    s = n * sizeof (uint32_t);

    for (i = 0; i < n; i++) {
        v1 = AV_RN32A(in1);
        v2 = AV_RN32A(in2);
        AV_WN32A(out, v1 ^ v2);
        in1 += 4;
        in2 += 4;
        out += 4;
    }
#endif

    n = size - s;

    for (i = 0; i < n; i++) {
        out[i] = in1[i] ^ in2[i];
    }
}

static int prompeg_create_bitstring(URLContext *h, const uint8_t *buf, int size,
        uint8_t **bitstring) {
    PrompegContext *s = h->priv_data;
    uint8_t *b;

    if (size < 12 || (buf[0] & 0xc0) != 0x80 || (buf[1] & 0x7f) != 0x21) {
        av_log(h, AV_LOG_ERROR, "Unsupported stream format (expected MPEG-TS over RTP)\n");
        return AVERROR(EINVAL);
    }
    if (size != s->packet_size) {
        av_log(h, AV_LOG_ERROR, "The RTP packet size must be constant (set pkt_size)\n");
        return AVERROR(EINVAL);
    }

    *bitstring = av_malloc(s->bitstring_size);
    if (!*bitstring) {
        av_log(h, AV_LOG_ERROR, "Failed to allocate the bitstring buffer\n");
        return AVERROR(ENOMEM);
    }
    b = *bitstring;

    // P, X, CC
    b[0] = buf[0] & 0x3f;
    // M, PT
    b[1] = buf[1];
    // Timestamp
    b[2] = buf[4];
    b[3] = buf[5];
    b[4] = buf[6];
    b[5] = buf[7];
    /*
     * length_recovery: the unsigned network-ordered sum of lengths of CSRC,
     * padding, extension and media payload
     */
    AV_WB16(b + 6, s->length_recovery);
    // Payload
    memcpy(b + 8, buf + 12, s->length_recovery);

    return 0;
}

static int prompeg_write_fec(URLContext *h, PrompegFec *fec, uint8_t type) {
    PrompegContext *s = h->priv_data;
    URLContext *hd;
    uint8_t *buf = s->rtp_buf; // zero-filled
    uint8_t *b = fec->bitstring;
    uint16_t sn;
    int ret;

    sn = type == PROMPEG_FEC_COL ? ++s->rtp_col_sn : ++s->rtp_row_sn;

    // V, P, X, CC
    buf[0] = 0x80 | (b[0] & 0x3f);
    // M, PT
    buf[1] = (b[1] & 0x80) | PROMPEG_RTP_PT;
    // SN
    AV_WB16(buf + 2, sn);
    // TS
    AV_WB32(buf + 4, fec->ts);
    // CSRC=0
    //AV_WB32(buf + 8, 0);
    // SNBase low bits
    AV_WB16(buf + 12, fec->sn);
    // Length recovery
    buf[14] = b[6];
    buf[15] = b[7];
    // E=1, PT recovery
    buf[16] = 0x80 | b[1];
    // Mask=0
    //buf[17] = 0x0;
    //buf[18] = 0x0;
    //buf[19] = 0x0;
    // TS recovery
    buf[20] = b[2];
    buf[21] = b[3];
    buf[22] = b[4];
    buf[23] = b[5];
    // X=0, D, type=0, index=0
    buf[24] = type == PROMPEG_FEC_COL ? 0x0 : 0x40;
    // offset
    buf[25] = type == PROMPEG_FEC_COL ? s->l : 0x1;
    // NA
    buf[26] = type == PROMPEG_FEC_COL ? s->d : s->l;
    // SNBase ext bits=0
    //buf[27] = 0x0;
    // Payload
    memcpy(buf + 28, b + 8, s->length_recovery);

    hd = type == PROMPEG_FEC_COL ? s->fec_col_hd : s->fec_row_hd;
    ret = ffurl_write(hd, buf, s->rtp_buf_size);
    return ret;
}

static int prompeg_open(URLContext *h, const char *uri, int flags) {
    PrompegContext *s = h->priv_data;
    AVDictionary *udp_opts = NULL;
    int rtp_port;
    char hostname[256];
    char buf[1024];

    s->fec_col_hd = NULL;
    s->fec_row_hd = NULL;

    if (s->l * s->d > 100) {
        av_log(h, AV_LOG_ERROR, "L * D must be <= 100\n");
        return AVERROR(EINVAL);
    }

    av_url_split(NULL, 0, NULL, 0, hostname, sizeof (hostname), &rtp_port,
            NULL, 0, uri);

    if (rtp_port < 1 || rtp_port > UINT16_MAX - 4) {
        av_log(h, AV_LOG_ERROR, "Invalid RTP base port %d\n", rtp_port);
        return AVERROR(EINVAL);
    }

    if (s->ttl > 0) {
        snprintf(buf, sizeof (buf), "%d", s->ttl);
        av_dict_set(&udp_opts, "ttl", buf, 0);
    }

    ff_url_join(buf, sizeof (buf), "udp", NULL, hostname, rtp_port + 2, NULL);
    if (ffurl_open_whitelist(&s->fec_col_hd, buf, flags, &h->interrupt_callback,
            &udp_opts, h->protocol_whitelist, h->protocol_blacklist, h) < 0)
        goto fail;
    ff_url_join(buf, sizeof (buf), "udp", NULL, hostname, rtp_port + 4, NULL);
    if (ffurl_open_whitelist(&s->fec_row_hd, buf, flags, &h->interrupt_callback,
            &udp_opts, h->protocol_whitelist, h->protocol_blacklist, h) < 0)
        goto fail;

    h->max_packet_size = s->fec_col_hd->max_packet_size;
    s->init = 1;

    av_dict_free(&udp_opts);
    av_log(h, AV_LOG_INFO, "ProMPEG CoP#3-R2 FEC L=%d D=%d\n", s->l, s->d);
    return 0;

fail:
    ffurl_closep(&s->fec_col_hd);
    ffurl_closep(&s->fec_row_hd);
    av_dict_free(&udp_opts);
    return AVERROR(EIO);
}

static int prompeg_init(URLContext *h, const uint8_t *buf, int size) {
    PrompegContext *s = h->priv_data;
    uint32_t seed;
    int i;

    s->fec_arr = NULL;
    s->rtp_buf = NULL;

    if (size < 12 || size > UINT16_MAX + 12) {
        av_log(h, AV_LOG_ERROR, "Invalid RTP packet size\n");
        return AVERROR_INVALIDDATA;
    }

    s->packet_idx = 0;
    s->packet_idx_max = s->l * s->d;
    s->packet_size = size;
    s->length_recovery = size - 12;
    s->rtp_buf_size = 28 + s->length_recovery; // 12 + 16: RTP + FEC headers
    s->bitstring_size = 8 + s->length_recovery; // 8: P, X, CC, M, PT, SN, TS
    s->fec_arr_len = 1 + 2 * s->l; // row + column tmp + column out

    if (h->flags & AVFMT_FLAG_BITEXACT) {
        s->rtp_col_sn = 0;
        s->rtp_row_sn = 0;
    } else {
        seed = av_get_random_seed();
        s->rtp_col_sn = seed & 0x0fff;
        s->rtp_row_sn = (seed >> 16) & 0x0fff;
    }

    s->fec_arr = av_malloc_array(s->fec_arr_len, sizeof (PrompegFec*));
    if (!s->fec_arr) {
        goto fail;
    }
    for (i = 0; i < s->fec_arr_len; i++) {
        s->fec_arr[i] = av_malloc(sizeof (PrompegFec));
        if (!s->fec_arr[i]) {
            goto fail;
        }
        s->fec_arr[i]->bitstring = av_malloc_array(s->bitstring_size, sizeof (uint8_t));
        if (!s->fec_arr[i]->bitstring) {
            av_freep(&s->fec_arr[i]);
            goto fail;
        }
    }
    s->fec_row = *s->fec_arr;
    s->fec_col = s->fec_arr + 1;
    s->fec_col_tmp = s->fec_arr + 1 + s->l;

    s->rtp_buf = av_malloc_array(s->rtp_buf_size, sizeof (uint8_t));
    if (!s->rtp_buf) {
        goto fail;
    }
    memset(s->rtp_buf, 0, s->rtp_buf_size);

    s->init = 0;
    s->first = 1;

    return 0;

fail:
    av_log(h, AV_LOG_ERROR, "Failed to allocate the FEC buffer\n");
    return AVERROR(ENOMEM);
}

static int prompeg_write(URLContext *h, const uint8_t *buf, int size) {
    PrompegContext *s = h->priv_data;
    PrompegFec *fec_tmp;
    uint8_t *bitstring = NULL;
    int col_idx, col_out_idx, row_idx;
    int ret, written = 0;

    if (s->init && ((ret = prompeg_init(h, buf, size)) < 0))
        goto end;

    if ((ret = prompeg_create_bitstring(h, buf, size, &bitstring)) < 0)
        goto end;

    col_idx = s->packet_idx % s->l;
    row_idx = s->packet_idx / s->l % s->d;

    // FEC' (row) send block-aligned, xor
    if (col_idx == 0) {
        if (!s->first || s->packet_idx > 0) {
            if ((ret = prompeg_write_fec(h, s->fec_row, PROMPEG_FEC_ROW)) < 0)
                goto end;
            written += ret;
        }
        memcpy(s->fec_row->bitstring, bitstring, s->bitstring_size);
        s->fec_row->sn = AV_RB16(buf + 2);
        s->fec_row->ts = AV_RB32(buf + 4);
    } else {
        xor_fast(s->fec_row->bitstring, bitstring, s->fec_row->bitstring,
                s->bitstring_size);
    }

    // FEC (column) xor
    if (row_idx == 0) {
        if (!s->first) {
            // swap fec_col and fec_col_tmp
            fec_tmp = s->fec_col[col_idx];
            s->fec_col[col_idx] = s->fec_col_tmp[col_idx];
            s->fec_col_tmp[col_idx] = fec_tmp;
        }
        memcpy(s->fec_col_tmp[col_idx]->bitstring, bitstring, s->bitstring_size);
        s->fec_col_tmp[col_idx]->sn = AV_RB16(buf + 2);
        s->fec_col_tmp[col_idx]->ts = AV_RB32(buf + 4);
    } else {
        xor_fast(s->fec_col_tmp[col_idx]->bitstring, bitstring,
                s->fec_col_tmp[col_idx]->bitstring, s->bitstring_size);
    }

    // FEC (column) send block-aligned
    if (!s->first && s->packet_idx % s->d == 0) {
        col_out_idx = s->packet_idx / s->d;
        if ((ret = prompeg_write_fec(h, s->fec_col[col_out_idx], PROMPEG_FEC_COL)) < 0)
            goto end;
        written += ret;
    }

    if (++s->packet_idx >= s->packet_idx_max) {
        s->packet_idx = 0;
        if (s->first)
            s->first = 0;
    }

    ret = written;

end:
    av_free(bitstring);
    return ret;
}

static int prompeg_close(URLContext *h) {
    PrompegContext *s = h->priv_data;
    int i;

    ffurl_closep(&s->fec_col_hd);
    ffurl_closep(&s->fec_row_hd);

    if (s->fec_arr) {
        for (i = 0; i < s->fec_arr_len; i++) {
            av_free(s->fec_arr[i]->bitstring);
            av_freep(&s->fec_arr[i]);
        }
        av_freep(&s->fec_arr);
    }
    av_freep(&s->rtp_buf);

    return 0;
}

const URLProtocol ff_prompeg_protocol = {
    .name                      = "prompeg",
    .url_open                  = prompeg_open,
    .url_write                 = prompeg_write,
    .url_close                 = prompeg_close,
    .priv_data_size            = sizeof(PrompegContext),
    .flags                     = URL_PROTOCOL_FLAG_NETWORK,
    .priv_data_class           = &prompeg_class,
};
