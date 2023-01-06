/*
 * MidiVid Archive codec
 *
 * Copyright (c) 2019 Paul B Mahol
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

#define CACHED_BITSTREAM_READER !ARCH_X86_32
#include "libavutil/intreadwrite.h"

#include "avcodec.h"
#include "codec_internal.h"
#include "decode.h"
#include "get_bits.h"
#include "lossless_videodsp.h"
#include "zlib_wrapper.h"

#include <zlib.h>

typedef struct MVHAContext {
    GetBitContext     gb;
    int nb_symbols;

    uint8_t           symb[256];
    uint32_t          prob[256];
    VLC               vlc;

    FFZStream         zstream;
    LLVidDSPContext   llviddsp;
} MVHAContext;

typedef struct Node {
    int16_t  sym;
    int16_t  n0;
    int16_t  l, r;
    uint32_t count;
} Node;

static void get_tree_codes(uint32_t *bits, int16_t *lens, uint8_t *xlat,
                           Node *nodes, int node,
                           uint32_t pfx, int pl, int *pos)
{
    int s;

    s = nodes[node].sym;
    if (s != -1) {
        bits[*pos] = (~pfx) & ((1ULL << FFMAX(pl, 1)) - 1);
        lens[*pos] = FFMAX(pl, 1);
        xlat[*pos] = s + (pl == 0);
        (*pos)++;
    } else {
        pfx <<= 1;
        pl++;
        get_tree_codes(bits, lens, xlat, nodes, nodes[node].l, pfx, pl,
                       pos);
        pfx |= 1;
        get_tree_codes(bits, lens, xlat, nodes, nodes[node].r, pfx, pl,
                       pos);
    }
}

static int build_vlc(AVCodecContext *avctx, VLC *vlc)
{
    MVHAContext *s = avctx->priv_data;
    Node nodes[512];
    uint32_t bits[256];
    int16_t lens[256];
    uint8_t xlat[256];
    int cur_node, i, j, pos = 0;

    ff_free_vlc(vlc);

    for (i = 0; i < s->nb_symbols; i++) {
        nodes[i].count = s->prob[i];
        nodes[i].sym   = s->symb[i];
        nodes[i].n0    = -2;
        nodes[i].l     = i;
        nodes[i].r     = i;
    }

    cur_node = s->nb_symbols;
    j = 0;
    do {
        for (i = 0; ; i++) {
            int new_node = j;
            int first_node = cur_node;
            int second_node = cur_node;
            unsigned nd, st;

            nodes[cur_node].count = -1;

            do {
                int val = nodes[new_node].count;
                if (val && (val < nodes[first_node].count)) {
                    if (val >= nodes[second_node].count) {
                        first_node = new_node;
                    } else {
                        first_node = second_node;
                        second_node = new_node;
                    }
                }
                new_node += 1;
            } while (new_node != cur_node);

            if (first_node == cur_node)
                break;

            nd = nodes[second_node].count;
            st = nodes[first_node].count;
            nodes[second_node].count = 0;
            nodes[first_node].count  = 0;
            if (nd >= UINT32_MAX - st) {
                av_log(avctx, AV_LOG_ERROR, "count overflow\n");
                return AVERROR_INVALIDDATA;
            }
            nodes[cur_node].count = nd + st;
            nodes[cur_node].sym = -1;
            nodes[cur_node].n0 = cur_node;
            nodes[cur_node].l = first_node;
            nodes[cur_node].r = second_node;
            cur_node++;
        }
        j++;
    } while (cur_node - s->nb_symbols == j);

    get_tree_codes(bits, lens, xlat, nodes, cur_node - 1, 0, 0, &pos);

    return ff_init_vlc_sparse(vlc, 12, pos, lens, 2, 2, bits, 4, 4, xlat, 1, 1, 0);
}

static int decode_frame(AVCodecContext *avctx, AVFrame *frame,
                        int *got_frame, AVPacket *avpkt)
{
    MVHAContext *s = avctx->priv_data;
    uint32_t type, size;
    int ret;

    if (avpkt->size <= 8)
        return AVERROR_INVALIDDATA;

    type = AV_RB32(avpkt->data);
    size = AV_RL32(avpkt->data + 4);

    if (size < 1 || size >= avpkt->size)
        return AVERROR_INVALIDDATA;

    if (type == MKTAG('L','Z','Y','V')) {
        z_stream *const zstream = &s->zstream.zstream;
        ret = inflateReset(zstream);
        if (ret != Z_OK) {
            av_log(avctx, AV_LOG_ERROR, "Inflate reset error: %d\n", ret);
            return AVERROR_EXTERNAL;
        }

        if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
            return ret;

        zstream->next_in  = avpkt->data + 8;
        zstream->avail_in = avpkt->size - 8;

        for (int p = 0; p < 3; p++) {
            for (int y = 0; y < avctx->height; y++) {
                zstream->next_out  = frame->data[p] + (avctx->height - y - 1) * frame->linesize[p];
                zstream->avail_out = avctx->width >> (p > 0);

                ret = inflate(zstream, Z_SYNC_FLUSH);
                if (ret != Z_OK && ret != Z_STREAM_END) {
                    av_log(avctx, AV_LOG_ERROR, "Inflate error: %d\n", ret);
                    return AVERROR_EXTERNAL;
                }
            }
        }
    } else if (type == MKTAG('H','U','F','Y')) {
        GetBitContext *gb = &s->gb;
        int first_symbol, symbol;

        ret = init_get_bits8(gb, avpkt->data + 8, avpkt->size - 8);
        if (ret < 0)
            return ret;

        skip_bits(gb, 24);

        first_symbol = get_bits(gb, 8);
        s->nb_symbols = get_bits(gb, 8) + 1;

        symbol = first_symbol;
        for (int i = 0; i < s->nb_symbols; symbol++) {
            int prob;

            if (get_bits_left(gb) < 4)
                return AVERROR_INVALIDDATA;

            if (get_bits1(gb)) {
                prob = get_bits(gb, 12);
            } else {
                prob = get_bits(gb, 3);
            }

            if (prob) {
                s->symb[i] = symbol;
                s->prob[i] = prob;
                i++;
            }
        }

        if (get_bits_left(gb) < avctx->height * avctx->width)
            return AVERROR_INVALIDDATA;

        ret = build_vlc(avctx, &s->vlc);
        if (ret < 0)
            return ret;

        if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
            return ret;

        for (int p = 0; p < 3; p++) {
            int width = avctx->width >> (p > 0);
            ptrdiff_t stride = frame->linesize[p];
            uint8_t *dst;

            dst = frame->data[p] + (avctx->height - 1) * frame->linesize[p];
            for (int y = 0; y < avctx->height; y++) {
                if (get_bits_left(gb) < width)
                    return AVERROR_INVALIDDATA;
                for (int x = 0; x < width; x++) {
                    int v = get_vlc2(gb, s->vlc.table, s->vlc.bits, 3);

                    if (v < 0)
                        return AVERROR_INVALIDDATA;

                    dst[x] = v;
                }
                dst -= stride;
            }
        }
    } else {
        return AVERROR_INVALIDDATA;
    }

    for (int p = 0; p < 3; p++) {
        int left, lefttop;
        int width = avctx->width >> (p > 0);
        ptrdiff_t stride = frame->linesize[p];
        uint8_t *dst;

        dst = frame->data[p] + (avctx->height - 1) * frame->linesize[p];
        s->llviddsp.add_left_pred(dst, dst, width, 0);
        if (avctx->height > 1) {
            dst -= stride;
            lefttop = left = dst[0];
            for (int y = 1; y < avctx->height; y++) {
                s->llviddsp.add_median_pred(dst, dst + stride, dst, width, &left, &lefttop);
                lefttop = left = dst[0];
                dst -= stride;
            }
        }
    }

    frame->pict_type = AV_PICTURE_TYPE_I;
    frame->key_frame = 1;
    *got_frame = 1;

    return avpkt->size;
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    MVHAContext *s = avctx->priv_data;

    avctx->pix_fmt = AV_PIX_FMT_YUV422P;

    ff_llviddsp_init(&s->llviddsp);

    return ff_inflate_init(&s->zstream, avctx);
}

static av_cold int decode_close(AVCodecContext *avctx)
{
    MVHAContext *s = avctx->priv_data;

    ff_inflate_end(&s->zstream);
    ff_free_vlc(&s->vlc);

    return 0;
}

const FFCodec ff_mvha_decoder = {
    .p.name           = "mvha",
    CODEC_LONG_NAME("MidiVid Archive Codec"),
    .p.type           = AVMEDIA_TYPE_VIDEO,
    .p.id             = AV_CODEC_ID_MVHA,
    .priv_data_size   = sizeof(MVHAContext),
    .init             = decode_init,
    .close            = decode_close,
    FF_CODEC_DECODE_CB(decode_frame),
    .p.capabilities   = AV_CODEC_CAP_DR1,
    .caps_internal    = FF_CODEC_CAP_INIT_CLEANUP,
};
