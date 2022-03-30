/*
 * JPEG-LS encoder
 * Copyright (c) 2003 Michael Niedermayer
 * Copyright (c) 2006 Konstantin Shishkov
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
 * JPEG-LS encoder.
 */

#define UNCHECKED_BITSTREAM_READER 1
#include "libavutil/opt.h"
#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "encode.h"
#include "get_bits.h"
#include "put_bits.h"
#include "put_golomb.h"
#include "mathops.h"
#include "mjpeg.h"
#include "jpegls.h"

typedef struct JPEGLSContext {
    AVClass *class;

    int pred;
    int comps;

    size_t size;
    uint8_t *buf;
} JPEGLSContext;

static inline void put_marker_byteu(PutByteContext *pb, enum JpegMarker code)
{
    bytestream2_put_byteu(pb, 0xff);
    bytestream2_put_byteu(pb, code);
}

/**
 * Encode error from regular symbol
 */
static inline void ls_encode_regular(JLSState *state, PutBitContext *pb, int Q,
                                     int err)
{
    int k;
    int val;
    int map;

    for (k = 0; (state->N[Q] << k) < state->A[Q]; k++)
        ;

    map = !state->near && !k && (2 * state->B[Q] <= -state->N[Q]);

    if (err < 0)
        err += state->range;
    if (err >= (state->range + 1 >> 1)) {
        err -= state->range;
        val  = 2 * FFABS(err) - 1 - map;
    } else
        val = 2 * err + map;

    set_ur_golomb_jpegls(pb, val, k, state->limit, state->qbpp);

    ff_jpegls_update_state_regular(state, Q, err);
}

/**
 * Encode error from run termination
 */
static inline void ls_encode_runterm(JLSState *state, PutBitContext *pb,
                                     int RItype, int err, int limit_add)
{
    int k;
    int val, map;
    int Q = 365 + RItype;
    int temp;

    temp = state->A[Q];
    if (RItype)
        temp += state->N[Q] >> 1;
    for (k = 0; (state->N[Q] << k) < temp; k++)
        ;
    map = 0;
    if (!k && err && (2 * state->B[Q] < state->N[Q]))
        map = 1;

    if (err < 0)
        val = -(2 * err) - 1 - RItype + map;
    else
        val = 2 * err - RItype - map;
    set_ur_golomb_jpegls(pb, val, k, state->limit - limit_add - 1, state->qbpp);

    if (err < 0)
        state->B[Q]++;
    state->A[Q] += (val + 1 - RItype) >> 1;

    ff_jpegls_downscale_state(state, Q);
}

/**
 * Encode run value as specified by JPEG-LS standard
 */
static inline void ls_encode_run(JLSState *state, PutBitContext *pb, int run,
                                 int comp, int trail)
{
    while (run >= (1 << ff_log2_run[state->run_index[comp]])) {
        put_bits(pb, 1, 1);
        run -= 1 << ff_log2_run[state->run_index[comp]];
        if (state->run_index[comp] < 31)
            state->run_index[comp]++;
    }
    /* if hit EOL, encode another full run, else encode aborted run */
    if (!trail && run) {
        put_bits(pb, 1, 1);
    } else if (trail) {
        put_bits(pb, 1, 0);
        if (ff_log2_run[state->run_index[comp]])
            put_bits(pb, ff_log2_run[state->run_index[comp]], run);
    }
}

/**
 * Encode one line of image
 */
static inline void ls_encode_line(JLSState *state, PutBitContext *pb,
                                  void *tmp, const void *in, int last2, int w,
                                  int stride, int comp, int bits)
{
    int x = 0;
    int Ra = R(tmp, 0), Rb, Rc = last2, Rd;
    int D0, D1, D2;

    while (x < w) {
        int err, pred, sign;

        /* compute gradients */
        Rb = R(tmp, x);
        Rd = (x >= w - stride) ? R(tmp, x) : R(tmp, x + stride);
        D0 = Rd - Rb;
        D1 = Rb - Rc;
        D2 = Rc - Ra;

        /* run mode */
        if ((FFABS(D0) <= state->near) &&
            (FFABS(D1) <= state->near) &&
            (FFABS(D2) <= state->near)) {
            int RUNval, RItype, run;

            run    = 0;
            RUNval = Ra;
            while (x < w && (FFABS(R(in, x) - RUNval) <= state->near)) {
                run++;
                W(tmp, x, Ra);
                x += stride;
            }
            ls_encode_run(state, pb, run, comp, x < w);
            if (x >= w)
                return;
            Rb     = R(tmp, x);
            RItype = FFABS(Ra - Rb) <= state->near;
            pred   = RItype ? Ra : Rb;
            err    = R(in, x) - pred;

            if (!RItype && Ra > Rb)
                err = -err;

            if (state->near) {
                if (err > 0)
                    err =  (state->near + err) / state->twonear;
                else
                    err = -(state->near - err) / state->twonear;

                if (RItype || (Rb >= Ra))
                    Ra = av_clip(pred + err * state->twonear, 0, state->maxval);
                else
                    Ra = av_clip(pred - err * state->twonear, 0, state->maxval);
            } else
                Ra = R(in, x);
            W(tmp, x, Ra);

            if (err < 0)
                err += state->range;
            if (err >= state->range + 1 >> 1)
                err -= state->range;

            ls_encode_runterm(state, pb, RItype, err,
                              ff_log2_run[state->run_index[comp]]);

            if (state->run_index[comp] > 0)
                state->run_index[comp]--;
        } else { /* regular mode */
            int context;

            context = ff_jpegls_quantize(state, D0) * 81 +
                      ff_jpegls_quantize(state, D1) *  9 +
                      ff_jpegls_quantize(state, D2);
            pred    = mid_pred(Ra, Ra + Rb - Rc, Rb);

            if (context < 0) {
                context = -context;
                sign    = 1;
                pred    = av_clip(pred - state->C[context], 0, state->maxval);
                err     = pred - R(in, x);
            } else {
                sign = 0;
                pred = av_clip(pred + state->C[context], 0, state->maxval);
                err  = R(in, x) - pred;
            }

            if (state->near) {
                if (err > 0)
                    err =  (state->near + err) / state->twonear;
                else
                    err = -(state->near - err) / state->twonear;
                if (!sign)
                    Ra = av_clip(pred + err * state->twonear, 0, state->maxval);
                else
                    Ra = av_clip(pred - err * state->twonear, 0, state->maxval);
            } else
                Ra = R(in, x);
            W(tmp, x, Ra);

            ls_encode_regular(state, pb, context, err);
        }
        Rc = Rb;
        x += stride;
    }
}

static void ls_store_lse(JLSState *state, PutByteContext *pb)
{
    /* Test if we have default params and don't need to store LSE */
    JLSState state2 = { 0 };
    state2.bpp  = state->bpp;
    state2.near = state->near;
    ff_jpegls_reset_coding_parameters(&state2, 1);
    if (state->T1 == state2.T1 &&
        state->T2 == state2.T2 &&
        state->T3 == state2.T3 &&
        state->reset == state2.reset)
        return;
    /* store LSE type 1 */
    put_marker_byteu(pb, LSE);
    bytestream2_put_be16u(pb, 13);
    bytestream2_put_byteu(pb, 1);
    bytestream2_put_be16u(pb, state->maxval);
    bytestream2_put_be16u(pb, state->T1);
    bytestream2_put_be16u(pb, state->T2);
    bytestream2_put_be16u(pb, state->T3);
    bytestream2_put_be16u(pb, state->reset);
}

static int encode_picture_ls(AVCodecContext *avctx, AVPacket *pkt,
                             const AVFrame *pict, int *got_packet)
{
    JPEGLSContext *ctx = avctx->priv_data;
    const AVFrame *const p = pict;
    PutByteContext pb;
    PutBitContext pb2;
    GetBitContext gb;
    const uint8_t *in;
    uint8_t *last = NULL;
    JLSState state = { 0 };
    size_t size;
    int i, ret, size_in_bits;
    int comps;

    last = av_mallocz(FFABS(p->linesize[0]));
    if (!last)
        return AVERROR(ENOMEM);

    init_put_bits(&pb2, ctx->buf, ctx->size);

    comps = ctx->comps;
    /* initialize JPEG-LS state from JPEG parameters */
    state.near = ctx->pred;
    state.bpp  = (avctx->pix_fmt == AV_PIX_FMT_GRAY16) ? 16 : 8;
    ff_jpegls_reset_coding_parameters(&state, 0);
    ff_jpegls_init_state(&state);

    in = p->data[0];
    if (avctx->pix_fmt == AV_PIX_FMT_GRAY8) {
        int t = 0;

        for (i = 0; i < avctx->height; i++) {
            int last0 = last[0];
            ls_encode_line(&state, &pb2, last, in, t, avctx->width, 1, 0, 8);
            t   = last0;
            in += p->linesize[0];
        }
    } else if (avctx->pix_fmt == AV_PIX_FMT_GRAY16) {
        int t = 0;

        for (i = 0; i < avctx->height; i++) {
            int last0 = *((uint16_t *)last);
            ls_encode_line(&state, &pb2, last, in, t, avctx->width, 1, 0, 16);
            t   = last0;
            in += p->linesize[0];
        }
    } else if (avctx->pix_fmt == AV_PIX_FMT_RGB24) {
        int j, width;
        int Rc[3] = { 0, 0, 0 };

        width = avctx->width * 3;
        for (i = 0; i < avctx->height; i++) {
            for (j = 0; j < 3; j++) {
                int last0 = last[j];
                ls_encode_line(&state, &pb2, last + j, in + j, Rc[j],
                               width, 3, j, 8);
                Rc[j] = last0;
            }
            in += p->linesize[0];
        }
    } else if (avctx->pix_fmt == AV_PIX_FMT_BGR24) {
        int j, width;
        int Rc[3] = { 0, 0, 0 };

        width = avctx->width * 3;
        for (i = 0; i < avctx->height; i++) {
            for (j = 2; j >= 0; j--) {
                int last0 = last[j];
                ls_encode_line(&state, &pb2, last + j, in + j, Rc[j],
                               width, 3, j, 8);
                Rc[j] = last0;
            }
            in += p->linesize[0];
        }
    }
    av_free(last);
    /* Now the actual image data has been written, which enables us to estimate
     * the needed packet size: For every 15 input bits, an escape bit might be
     * added below; and if put_bits_count % 15 is >= 8, then another bit might
     * be added.
     * Furthermore the specification says that after doing 0xff escaping unused
     * bits in the last byte must be set to 0, so just append 7 "optional" zero
     * bits to avoid special-casing. This also simplifies the size calculation:
     * Properly rounding up is now automatically baked-in. */
    put_bits(&pb2, 7, 0);
    /* Make sure that the bit count + padding is representable in an int;
       necessary for put_bits_count() as well as for using a GetBitContext. */
    if (put_bytes_count(&pb2, 0) > INT_MAX / 8 - AV_INPUT_BUFFER_PADDING_SIZE)
        return AVERROR(ERANGE);
    size_in_bits = put_bits_count(&pb2);
    flush_put_bits(&pb2);
    size  = size_in_bits * 2U / 15;
    size += 2 + 2 + 2 + 1 + 2 + 2 + 1 + comps * (1 + 1 + 1) + 2 + 2 + 1
            + comps * (1 + 1) + 1 + 1 + 1; /* Header */
    size += 2 + 2 + 1 + 2 + 2 + 2 + 2 + 2; /* LSE */
    size += 2; /* EOI */
    if ((ret = ff_get_encode_buffer(avctx, pkt, size, 0)) < 0)
        return ret;

    bytestream2_init_writer(&pb, pkt->data, pkt->size);

    /* write our own JPEG header, can't use mjpeg_picture_header */
    put_marker_byteu(&pb, SOI);
    put_marker_byteu(&pb, SOF48);
    bytestream2_put_be16u(&pb, 8 + comps * 3); // header size depends on components
    bytestream2_put_byteu(&pb, (avctx->pix_fmt == AV_PIX_FMT_GRAY16) ? 16 : 8);  // bpp
    bytestream2_put_be16u(&pb, avctx->height);
    bytestream2_put_be16u(&pb, avctx->width);
    bytestream2_put_byteu(&pb, comps);          // components
    for (i = 1; i <= comps; i++) {
        bytestream2_put_byteu(&pb, i);     // component ID
        bytestream2_put_byteu(&pb, 0x11);  // subsampling: none
        bytestream2_put_byteu(&pb, 0);     // Tiq, used by JPEG-LS ext
    }

    put_marker_byteu(&pb, SOS);
    bytestream2_put_be16u(&pb, 6 + comps * 2);
    bytestream2_put_byteu(&pb, comps);
    for (i = 1; i <= comps; i++) {
        bytestream2_put_byteu(&pb, i);   // component ID
        bytestream2_put_byteu(&pb, 0);   // mapping index: none
    }
    bytestream2_put_byteu(&pb, ctx->pred);
    bytestream2_put_byteu(&pb, (comps > 1) ? 1 : 0);  // interleaving: 0 - plane, 1 - line
    bytestream2_put_byteu(&pb, 0);  // point transform: none

    ls_store_lse(&state, &pb);

    /* do escape coding */
    init_get_bits(&gb, pb2.buf, size_in_bits);
    size_in_bits -= 7;
    while (get_bits_count(&gb) < size_in_bits) {
        int v;
        v = get_bits(&gb, 8);
        bytestream2_put_byteu(&pb, v);
        if (v == 0xFF) {
            v = get_bits(&gb, 7);
            bytestream2_put_byteu(&pb, v);
        }
    }

    /* End of image */
    put_marker_byteu(&pb, EOI);

    emms_c();

    av_shrink_packet(pkt, bytestream2_tell_p(&pb));
    *got_packet = 1;
    return 0;
}

static av_cold int encode_jpegls_init(AVCodecContext *avctx)
{
    JPEGLSContext *ctx = avctx->priv_data;
    size_t size;

    if ((avctx->width | avctx->height) > UINT16_MAX) {
        av_log(avctx, AV_LOG_ERROR, "Dimensions exceeding 65535x65535\n");
        return AVERROR(EINVAL);
    }
    if (avctx->pix_fmt == AV_PIX_FMT_GRAY8 ||
        avctx->pix_fmt == AV_PIX_FMT_GRAY16)
        ctx->comps = 1;
    else
        ctx->comps = 3;
    size = AV_INPUT_BUFFER_MIN_SIZE;
    /* INT_MAX due to PutBit-API. */
    if (avctx->width * (unsigned)avctx->height > (INT_MAX - size) / 4 / ctx->comps)
        return AVERROR(ERANGE);
    size += 4 * ctx->comps * avctx->width * avctx->height;
    ctx->size = size;
    ctx->buf = av_malloc(size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!ctx->buf)
        return AVERROR(ENOMEM);

    return 0;
}

static av_cold int encode_jpegls_close(AVCodecContext *avctx)
{
    JPEGLSContext *ctx = avctx->priv_data;

    av_freep(&ctx->buf);
    return 0;
}

#define OFFSET(x) offsetof(JPEGLSContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
{ "pred", "Prediction method", OFFSET(pred), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 2, VE, "pred" },
    { "left",   NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 0 }, INT_MIN, INT_MAX, VE, "pred" },
    { "plane",  NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 1 }, INT_MIN, INT_MAX, VE, "pred" },
    { "median", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 2 }, INT_MIN, INT_MAX, VE, "pred" },

    { NULL},
};

static const AVClass jpegls_class = {
    .class_name = "jpegls",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_jpegls_encoder = {
    .p.name         = "jpegls",
    .p.long_name    = NULL_IF_CONFIG_SMALL("JPEG-LS"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_JPEGLS,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_FRAME_THREADS,
    .priv_data_size = sizeof(JPEGLSContext),
    .p.priv_class   = &jpegls_class,
    .init           = encode_jpegls_init,
    FF_CODEC_ENCODE_CB(encode_picture_ls),
    .close          = encode_jpegls_close,
    .p.pix_fmts     = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_BGR24, AV_PIX_FMT_RGB24,
        AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY16,
        AV_PIX_FMT_NONE
    },
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE |
                      FF_CODEC_CAP_INIT_CLEANUP,
};
