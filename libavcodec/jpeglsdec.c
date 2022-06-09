/*
 * JPEG-LS decoder
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
 * JPEG-LS decoder.
 */

#include "avcodec.h"
#include "codec_internal.h"
#include "get_bits.h"
#include "golomb.h"
#include "mathops.h"
#include "mjpegdec.h"
#include "jpegls.h"
#include "jpeglsdec.h"

/*
 * Uncomment this to significantly speed up decoding of broken JPEG-LS
 * (or test broken JPEG-LS decoder) and slow down ordinary decoding a bit.
 *
 * There is no Golomb code with length >= 32 bits possible, so check and
 * avoid situation of 32 zeros, FFmpeg Golomb decoder is painfully slow
 * on this errors.
 */
//#define JLS_BROKEN

/**
 * Decode LSE block with initialization parameters
 */
int ff_jpegls_decode_lse(MJpegDecodeContext *s)
{
    int id;
    int tid, wt, maxtab, i, j;

    int len = get_bits(&s->gb, 16);
    id = get_bits(&s->gb, 8);

    switch (id) {
    case 1:
        if (len < 13)
            return AVERROR_INVALIDDATA;

        s->maxval = get_bits(&s->gb, 16);
        s->t1     = get_bits(&s->gb, 16);
        s->t2     = get_bits(&s->gb, 16);
        s->t3     = get_bits(&s->gb, 16);
        s->reset  = get_bits(&s->gb, 16);

        if (s->avctx->debug & FF_DEBUG_PICT_INFO) {
            av_log(s->avctx, AV_LOG_DEBUG, "Coding parameters maxval:%d T1:%d T2:%d T3:%d reset:%d\n",
                   s->maxval, s->t1, s->t2, s->t3, s->reset);
        }

//        ff_jpegls_reset_coding_parameters(s, 0);
        //FIXME quant table?
        break;
    case 2:
        s->palette_index = 0;
    case 3:
        tid= get_bits(&s->gb, 8);
        wt = get_bits(&s->gb, 8);

        if (len < 5)
            return AVERROR_INVALIDDATA;

        if (wt < 1 || wt > MAX_COMPONENTS) {
            avpriv_request_sample(s->avctx, "wt %d", wt);
            return AVERROR_PATCHWELCOME;
        }

        if (!s->maxval)
            maxtab = 255;
        else if ((5 + wt*(s->maxval+1)) < 65535)
            maxtab = s->maxval;
        else
            maxtab = 65530/wt - 1;

        if (s->avctx->debug & FF_DEBUG_PICT_INFO) {
            av_log(s->avctx, AV_LOG_DEBUG, "LSE palette %d tid:%d wt:%d maxtab:%d\n", id, tid, wt, maxtab);
        }
        if (maxtab >= 256) {
            avpriv_request_sample(s->avctx, ">8bit palette");
            return AVERROR_PATCHWELCOME;
        }
        maxtab = FFMIN(maxtab, (len - 5) / wt + s->palette_index);

        if (s->palette_index > maxtab)
            return AVERROR_INVALIDDATA;

        if ((s->avctx->pix_fmt == AV_PIX_FMT_GRAY8 || s->avctx->pix_fmt == AV_PIX_FMT_PAL8) &&
            (s->picture_ptr->format == AV_PIX_FMT_GRAY8 || s->picture_ptr->format == AV_PIX_FMT_PAL8)) {
            uint32_t *pal = (uint32_t *)s->picture_ptr->data[1];
            int shift = 0;

            if (s->avctx->bits_per_raw_sample > 0 && s->avctx->bits_per_raw_sample < 8) {
                maxtab = FFMIN(maxtab, (1<<s->avctx->bits_per_raw_sample)-1);
                shift = 8 - s->avctx->bits_per_raw_sample;
            }

            s->force_pal8++;
            if (!pal) {
                if (s->force_pal8 > 1)
                    return AVERROR_INVALIDDATA;
                return 1;
            }

            for (i=s->palette_index; i<=maxtab; i++) {
                uint8_t k = i << shift;
                pal[k] = wt < 4 ? 0xFF000000 : 0;
                for (j=0; j<wt; j++) {
                    pal[k] |= get_bits(&s->gb, 8) << (8*(wt-j-1));
                }
            }
            s->palette_index = i;
        }
        break;
    case 4:
        avpriv_request_sample(s->avctx, "oversize image");
        return AVERROR(ENOSYS);
    default:
        av_log(s->avctx, AV_LOG_ERROR, "invalid id %d\n", id);
        return AVERROR_INVALIDDATA;
    }
    ff_dlog(s->avctx, "ID=%i, T=%i,%i,%i\n", id, s->t1, s->t2, s->t3);

    return 0;
}

/**
 * Get context-dependent Golomb code, decode it and update context
 */
static inline int ls_get_code_regular(GetBitContext *gb, JLSState *state, int Q)
{
    int k, ret;

    for (k = 0; ((unsigned)state->N[Q] << k) < state->A[Q]; k++)
        ;

#ifdef JLS_BROKEN
    if (!show_bits_long(gb, 32))
        return -1;
#endif
    ret = get_ur_golomb_jpegls(gb, k, state->limit, state->qbpp);

    /* decode mapped error */
    if (ret & 1)
        ret = -(ret + 1 >> 1);
    else
        ret >>= 1;

    /* for NEAR=0, k=0 and 2*B[Q] <= - N[Q] mapping is reversed */
    if (!state->near && !k && (2 * state->B[Q] <= -state->N[Q]))
        ret = -(ret + 1);

    ret = ff_jpegls_update_state_regular(state, Q, ret);

    return ret;
}

/**
 * Get Golomb code, decode it and update state for run termination
 */
static inline int ls_get_code_runterm(GetBitContext *gb, JLSState *state,
                                      int RItype, int limit_add)
{
    int k, ret, temp, map;
    int Q = 365 + RItype;

    temp = state->A[Q];
    if (RItype)
        temp += state->N[Q] >> 1;

    for (k = 0; ((unsigned)state->N[Q] << k) < temp; k++)
        ;

#ifdef JLS_BROKEN
    if (!show_bits_long(gb, 32))
        return -1;
#endif
    ret = get_ur_golomb_jpegls(gb, k, state->limit - limit_add - 1,
                               state->qbpp);
    if (ret < 0)
        return -0x10000;

    /* decode mapped error */
    map = 0;
    if (!k && (RItype || ret) && (2 * state->B[Q] < state->N[Q]))
        map = 1;
    ret += RItype + map;

    if (ret & 1) {
        ret = map - (ret + 1 >> 1);
        state->B[Q]++;
    } else {
        ret = ret >> 1;
    }

    if (FFABS(ret) > 0xFFFF)
        return -0x10000;
    /* update state */
    state->A[Q] += FFABS(ret) - RItype;
    ret         *= state->twonear;
    ff_jpegls_downscale_state(state, Q);

    return ret;
}

/**
 * Decode one line of image
 */
static inline int ls_decode_line(JLSState *state, MJpegDecodeContext *s,
                                  void *last, void *dst, int last2, int w,
                                  int stride, int comp, int bits)
{
    int i, x = 0;
    int Ra, Rb, Rc, Rd;
    int D0, D1, D2;

    while (x < w) {
        int err, pred;

        if (get_bits_left(&s->gb) <= 0)
            return AVERROR_INVALIDDATA;

        /* compute gradients */
        Ra = x ? R(dst, x - stride) : R(last, x);
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
            int r;
            int RItype;

            /* decode full runs while available */
            while (get_bits1(&s->gb)) {
                int r;
                r = 1 << ff_log2_run[state->run_index[comp]];
                if (x + r * stride > w)
                    r = (w - x) / stride;
                for (i = 0; i < r; i++) {
                    W(dst, x, Ra);
                    x += stride;
                }
                /* if EOL reached, we stop decoding */
                if (r != 1 << ff_log2_run[state->run_index[comp]])
                    return 0;
                if (state->run_index[comp] < 31)
                    state->run_index[comp]++;
                if (x + stride > w)
                    return 0;
            }
            /* decode aborted run */
            r = ff_log2_run[state->run_index[comp]];
            if (r)
                r = get_bits(&s->gb, r);
            if (x + r * stride > w) {
                r = (w - x) / stride;
            }
            for (i = 0; i < r; i++) {
                W(dst, x, Ra);
                x += stride;
            }

            if (x >= w) {
                av_log(NULL, AV_LOG_ERROR, "run overflow\n");
                av_assert0(x <= w);
                return AVERROR_INVALIDDATA;
            }

            /* decode run termination value */
            Rb     = R(last, x);
            RItype = (FFABS(Ra - Rb) <= state->near) ? 1 : 0;
            err    = ls_get_code_runterm(&s->gb, state, RItype,
                                         ff_log2_run[state->run_index[comp]]);
            if (state->run_index[comp])
                state->run_index[comp]--;

            if (state->near && RItype) {
                pred = Ra + err;
            } else {
                if (Rb < Ra)
                    pred = Rb - err;
                else
                    pred = Rb + err;
            }
        } else { /* regular mode */
            int context, sign;

            context = ff_jpegls_quantize(state, D0) * 81 +
                      ff_jpegls_quantize(state, D1) *  9 +
                      ff_jpegls_quantize(state, D2);
            pred    = mid_pred(Ra, Ra + Rb - Rc, Rb);

            if (context < 0) {
                context = -context;
                sign    = 1;
            } else {
                sign = 0;
            }

            if (sign) {
                pred = av_clip(pred - state->C[context], 0, state->maxval);
                err  = -ls_get_code_regular(&s->gb, state, context);
            } else {
                pred = av_clip(pred + state->C[context], 0, state->maxval);
                err  = ls_get_code_regular(&s->gb, state, context);
            }

            /* we have to do something more for near-lossless coding */
            pred += err;
        }
        if (state->near) {
            if (pred < -state->near)
                pred += state->range * state->twonear;
            else if (pred > state->maxval + state->near)
                pred -= state->range * state->twonear;
            pred = av_clip(pred, 0, state->maxval);
        }

        pred &= state->maxval;
        W(dst, x, pred);
        x += stride;
    }

    return 0;
}

int ff_jpegls_decode_picture(MJpegDecodeContext *s, int near,
                             int point_transform, int ilv)
{
    int i, t = 0;
    uint8_t *zero, *last, *cur;
    JLSState *state = s->jls_state;
    int off = 0, stride = 1, width, shift, ret = 0;
    int decoded_height = 0;

    if (!state) {
        state = av_malloc(sizeof(*state));
        if (!state)
            return AVERROR(ENOMEM);
        s->jls_state = state;
    }
    zero = av_mallocz(s->picture_ptr->linesize[0]);
    if (!zero)
        return AVERROR(ENOMEM);
    last = zero;
    cur  = s->picture_ptr->data[0];

    /* initialize JPEG-LS state from JPEG parameters */
    memset(state, 0, sizeof(*state));
    state->near   = near;
    state->bpp    = (s->bits < 2) ? 2 : s->bits;
    state->maxval = s->maxval;
    state->T1     = s->t1;
    state->T2     = s->t2;
    state->T3     = s->t3;
    state->reset  = s->reset;
    ff_jpegls_reset_coding_parameters(state, 0);
    ff_jpegls_init_state(state);

    if (s->bits <= 8)
        shift = point_transform + (8 - s->bits);
    else
        shift = point_transform + (16 - s->bits);

    if (shift >= 16) {
        ret = AVERROR_INVALIDDATA;
        goto end;
    }

    if (s->avctx->debug & FF_DEBUG_PICT_INFO) {
        av_log(s->avctx, AV_LOG_DEBUG,
               "JPEG-LS params: %ix%i NEAR=%i MV=%i T(%i,%i,%i) "
               "RESET=%i, LIMIT=%i, qbpp=%i, RANGE=%i\n",
                s->width, s->height, state->near, state->maxval,
                state->T1, state->T2, state->T3,
                state->reset, state->limit, state->qbpp, state->range);
        av_log(s->avctx, AV_LOG_DEBUG, "JPEG params: ILV=%i Pt=%i BPP=%i, scan = %i\n",
                ilv, point_transform, s->bits, s->cur_scan);
    }
    if (get_bits_left(&s->gb) < s->height) {
        ret = AVERROR_INVALIDDATA;
        goto end;
    }
    if (ilv == 0) { /* separate planes */
        if (s->cur_scan > s->nb_components) {
            ret = AVERROR_INVALIDDATA;
            goto end;
        }
        stride = (s->nb_components > 1) ? 3 : 1;
        off    = av_clip(s->cur_scan - 1, 0, stride - 1);
        width  = s->width * stride;
        cur   += off;
        for (i = 0; i < s->height; i++) {
            int ret;
            if (s->bits <= 8) {
                ret = ls_decode_line(state, s, last, cur, t, width, stride, off, 8);
                t = last[0];
            } else {
                ret = ls_decode_line(state, s, last, cur, t, width, stride, off, 16);
                t = *((uint16_t *)last);
            }
            if (ret < 0)
                break;
            last = cur;
            cur += s->picture_ptr->linesize[0];

            if (s->restart_interval && !--s->restart_count) {
                align_get_bits(&s->gb);
                skip_bits(&s->gb, 16); /* skip RSTn */
            }
        }
        decoded_height = i;
    } else if (ilv == 1) { /* line interleaving */
        int j;
        int Rc[3] = { 0, 0, 0 };
        stride = (s->nb_components > 1) ? 3 : 1;
        memset(cur, 0, s->picture_ptr->linesize[0]);
        width = s->width * stride;
        for (i = 0; i < s->height; i++) {
            int ret;
            for (j = 0; j < stride; j++) {
                ret = ls_decode_line(state, s, last + j, cur + j,
                               Rc[j], width, stride, j, 8);
                if (ret < 0)
                    break;
                Rc[j] = last[j];

                if (s->restart_interval && !--s->restart_count) {
                    align_get_bits(&s->gb);
                    skip_bits(&s->gb, 16); /* skip RSTn */
                }
            }
            if (ret < 0)
                break;
            last = cur;
            cur += s->picture_ptr->linesize[0];
        }
        decoded_height = i;
    } else if (ilv == 2) { /* sample interleaving */
        avpriv_report_missing_feature(s->avctx, "Sample interleaved images");
        ret = AVERROR_PATCHWELCOME;
        goto end;
    } else { /* unknown interleaving */
        avpriv_report_missing_feature(s->avctx, "Unknown interleaved images");
        ret = AVERROR_PATCHWELCOME;
        goto end;
    }

    if (s->xfrm && s->nb_components == 3) {
        int x, w;

        w = s->width * s->nb_components;

        if (s->bits <= 8) {
            uint8_t *src = s->picture_ptr->data[0];

            for (i = 0; i < s->height; i++) {
                switch(s->xfrm) {
                case 1:
                    for (x = off; x + 2 < w; x += 3) {
                        src[x  ] += src[x+1] + 128;
                        src[x+2] += src[x+1] + 128;
                    }
                    break;
                case 2:
                    for (x = off; x + 2 < w; x += 3) {
                        src[x  ] += src[x+1] + 128;
                        src[x+2] += ((src[x  ] + src[x+1])>>1) + 128;
                    }
                    break;
                case 3:
                    for (x = off; x + 2 < w; x += 3) {
                        int g = src[x+0] - ((src[x+2]+src[x+1])>>2) + 64;
                        src[x+0] = src[x+2] + g + 128;
                        src[x+2] = src[x+1] + g + 128;
                        src[x+1] = g;
                    }
                    break;
                case 4:
                    for (x = off; x + 2 < w; x += 3) {
                        int r    = src[x+0] - ((                       359 * (src[x+2]-128) + 490) >> 8);
                        int g    = src[x+0] - (( 88 * (src[x+1]-128) - 183 * (src[x+2]-128) +  30) >> 8);
                        int b    = src[x+0] + ((454 * (src[x+1]-128)                        + 574) >> 8);
                        src[x+0] = av_clip_uint8(r);
                        src[x+1] = av_clip_uint8(g);
                        src[x+2] = av_clip_uint8(b);
                    }
                    break;
                }
                src += s->picture_ptr->linesize[0];
            }
        }else
            avpriv_report_missing_feature(s->avctx, "16bit xfrm");
    }

    if (shift) { /* we need to do point transform or normalize samples */
        int x, w;

        w = s->width * s->nb_components;

        if (s->bits <= 8) {
            uint8_t *src = s->picture_ptr->data[0];

            for (i = 0; i < decoded_height; i++) {
                for (x = off; x < w; x += stride)
                    src[x] <<= shift;
                src += s->picture_ptr->linesize[0];
            }
        } else {
            uint16_t *src = (uint16_t *)s->picture_ptr->data[0];

            for (i = 0; i < decoded_height; i++) {
                for (x = 0; x < w; x++)
                    src[x] <<= shift;
                src += s->picture_ptr->linesize[0] / 2;
            }
        }
    }

end:
    av_free(zero);

    return ret;
}

const FFCodec ff_jpegls_decoder = {
    .p.name         = "jpegls",
    .p.long_name    = NULL_IF_CONFIG_SMALL("JPEG-LS"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_JPEGLS,
    .priv_data_size = sizeof(MJpegDecodeContext),
    .init           = ff_mjpeg_decode_init,
    .close          = ff_mjpeg_decode_end,
    FF_CODEC_RECEIVE_FRAME_CB(ff_mjpeg_receive_frame),
    .p.capabilities = AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP |
                      FF_CODEC_CAP_SETS_PKT_DTS,
};
