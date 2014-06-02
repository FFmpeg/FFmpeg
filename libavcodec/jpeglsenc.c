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

#include "avcodec.h"
#include "get_bits.h"
#include "put_bits.h"
#include "golomb.h"
#include "internal.h"
#include "mathops.h"
#include "mjpeg.h"
#include "jpegls.h"

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
                                  void *last, void *cur, int last2, int w,
                                  int stride, int comp, int bits)
{
    int x = 0;
    int Ra, Rb, Rc, Rd;
    int D0, D1, D2;

    while (x < w) {
        int err, pred, sign;

        /* compute gradients */
        Ra = x ? R(cur, x - stride) : R(last, x);
        Rb = R(last, x);
        Rc = x ? R(last, x - stride) : last2;
        Rd = (x >= w - stride) ? R(last, x) : R(last, x + stride);
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
            while (x < w && (FFABS(R(cur, x) - RUNval) <= state->near)) {
                run++;
                W(cur, x, Ra);
                x += stride;
            }
            ls_encode_run(state, pb, run, comp, x < w);
            if (x >= w)
                return;
            Rb     = R(last, x);
            RItype = FFABS(Ra - Rb) <= state->near;
            pred   = RItype ? Ra : Rb;
            err    = R(cur, x) - pred;

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
                W(cur, x, Ra);
            }
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
                err     = pred - R(cur, x);
            } else {
                sign = 0;
                pred = av_clip(pred + state->C[context], 0, state->maxval);
                err  = R(cur, x) - pred;
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
                W(cur, x, Ra);
            }

            ls_encode_regular(state, pb, context, err);
        }
        x += stride;
    }
}

static void ls_store_lse(JLSState *state, PutBitContext *pb)
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
    put_marker(pb, LSE);
    put_bits(pb, 16, 13);
    put_bits(pb, 8, 1);
    put_bits(pb, 16, state->maxval);
    put_bits(pb, 16, state->T1);
    put_bits(pb, 16, state->T2);
    put_bits(pb, 16, state->T3);
    put_bits(pb, 16, state->reset);
}

static int encode_picture_ls(AVCodecContext *avctx, AVPacket *pkt,
                             const AVFrame *pict, int *got_packet)
{
    const AVFrame *const p = pict;
    const int near         = avctx->prediction_method;
    PutBitContext pb, pb2;
    GetBitContext gb;
    uint8_t *buf2, *zero, *cur, *last;
    JLSState *state;
    int i, size, ret;
    int comps;

    if (avctx->pix_fmt == AV_PIX_FMT_GRAY8 ||
        avctx->pix_fmt == AV_PIX_FMT_GRAY16)
        comps = 1;
    else
        comps = 3;

    if ((ret = ff_alloc_packet2(avctx, pkt, avctx->width  *avctx->height * comps * 4 +
                                FF_MIN_BUFFER_SIZE)) < 0)
        return ret;

    buf2 = av_malloc(pkt->size);

    init_put_bits(&pb, pkt->data, pkt->size);
    init_put_bits(&pb2, buf2, pkt->size);

    /* write our own JPEG header, can't use mjpeg_picture_header */
    put_marker(&pb, SOI);
    put_marker(&pb, SOF48);
    put_bits(&pb, 16, 8 + comps * 3); // header size depends on components
    put_bits(&pb, 8, (avctx->pix_fmt == AV_PIX_FMT_GRAY16) ? 16 : 8);  // bpp
    put_bits(&pb, 16, avctx->height);
    put_bits(&pb, 16, avctx->width);
    put_bits(&pb, 8, comps);          // components
    for (i = 1; i <= comps; i++) {
        put_bits(&pb, 8, i);     // component ID
        put_bits(&pb, 8, 0x11);  // subsampling: none
        put_bits(&pb, 8, 0);     // Tiq, used by JPEG-LS ext
    }

    put_marker(&pb, SOS);
    put_bits(&pb, 16, 6 + comps * 2);
    put_bits(&pb, 8, comps);
    for (i = 1; i <= comps; i++) {
        put_bits(&pb, 8, i);   // component ID
        put_bits(&pb, 8, 0);   // mapping index: none
    }
    put_bits(&pb, 8, near);
    put_bits(&pb, 8, (comps > 1) ? 1 : 0);  // interleaving: 0 - plane, 1 - line
    put_bits(&pb, 8, 0);  // point transform: none

    state = av_mallocz(sizeof(JLSState));
    /* initialize JPEG-LS state from JPEG parameters */
    state->near = near;
    state->bpp  = (avctx->pix_fmt == AV_PIX_FMT_GRAY16) ? 16 : 8;
    ff_jpegls_reset_coding_parameters(state, 0);
    ff_jpegls_init_state(state);

    ls_store_lse(state, &pb);

    zero = av_mallocz(FFABS(p->linesize[0]));
    if (!zero) {
        av_free(state);
        return AVERROR(ENOMEM);
    }
    last = zero;
    cur  = p->data[0];
    if (avctx->pix_fmt == AV_PIX_FMT_GRAY8) {
        int t = 0;

        for (i = 0; i < avctx->height; i++) {
            ls_encode_line(state, &pb2, last, cur, t, avctx->width, 1, 0, 8);
            t    = last[0];
            last = cur;
            cur += p->linesize[0];
        }
    } else if (avctx->pix_fmt == AV_PIX_FMT_GRAY16) {
        int t = 0;

        for (i = 0; i < avctx->height; i++) {
            ls_encode_line(state, &pb2, last, cur, t, avctx->width, 1, 0, 16);
            t    = *((uint16_t *)last);
            last = cur;
            cur += p->linesize[0];
        }
    } else if (avctx->pix_fmt == AV_PIX_FMT_RGB24) {
        int j, width;
        int Rc[3] = { 0, 0, 0 };

        width = avctx->width * 3;
        for (i = 0; i < avctx->height; i++) {
            for (j = 0; j < 3; j++) {
                ls_encode_line(state, &pb2, last + j, cur + j, Rc[j],
                               width, 3, j, 8);
                Rc[j] = last[j];
            }
            last = cur;
            cur += p->linesize[0];
        }
    } else if (avctx->pix_fmt == AV_PIX_FMT_BGR24) {
        int j, width;
        int Rc[3] = { 0, 0, 0 };

        width = avctx->width * 3;
        for (i = 0; i < avctx->height; i++) {
            for (j = 2; j >= 0; j--) {
                ls_encode_line(state, &pb2, last + j, cur + j, Rc[j],
                               width, 3, j, 8);
                Rc[j] = last[j];
            }
            last = cur;
            cur += p->linesize[0];
        }
    }

    av_freep(&zero);
    av_freep(&state);

    /* the specification says that after doing 0xff escaping unused bits in
     * the last byte must be set to 0, so just append 7 "optional" zero-bits
     * to avoid special-casing. */
    put_bits(&pb2, 7, 0);
    size = put_bits_count(&pb2);
    flush_put_bits(&pb2);
    /* do escape coding */
    init_get_bits(&gb, buf2, size);
    size -= 7;
    while (get_bits_count(&gb) < size) {
        int v;
        v = get_bits(&gb, 8);
        put_bits(&pb, 8, v);
        if (v == 0xFF) {
            v = get_bits(&gb, 7);
            put_bits(&pb, 8, v);
        }
    }
    avpriv_align_put_bits(&pb);
    av_free(buf2);

    /* End of image */
    put_marker(&pb, EOI);
    flush_put_bits(&pb);

    emms_c();

    pkt->size   = put_bits_count(&pb) >> 3;
    pkt->flags |= AV_PKT_FLAG_KEY;
    *got_packet = 1;
    return 0;
}

static av_cold int encode_close(AVCodecContext *avctx)
{
    av_frame_free(&avctx->coded_frame);
    return 0;
}

static av_cold int encode_init_ls(AVCodecContext *ctx)
{
    ctx->coded_frame = av_frame_alloc();
    if (!ctx->coded_frame)
        return AVERROR(ENOMEM);

    ctx->coded_frame->pict_type = AV_PICTURE_TYPE_I;
    ctx->coded_frame->key_frame = 1;

    if (ctx->pix_fmt != AV_PIX_FMT_GRAY8  &&
        ctx->pix_fmt != AV_PIX_FMT_GRAY16 &&
        ctx->pix_fmt != AV_PIX_FMT_RGB24  &&
        ctx->pix_fmt != AV_PIX_FMT_BGR24) {
        av_log(ctx, AV_LOG_ERROR,
               "Only grayscale and RGB24/BGR24 images are supported\n");
        return -1;
    }
    return 0;
}

AVCodec ff_jpegls_encoder = {
    .name           = "jpegls",
    .long_name      = NULL_IF_CONFIG_SMALL("JPEG-LS"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_JPEGLS,
    .init           = encode_init_ls,
    .close          = encode_close,
    .capabilities   = CODEC_CAP_FRAME_THREADS | CODEC_CAP_INTRA_ONLY,
    .encode2        = encode_picture_ls,
    .pix_fmts       = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_BGR24, AV_PIX_FMT_RGB24,
        AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY16,
        AV_PIX_FMT_NONE
    },
};
