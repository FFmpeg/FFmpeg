/*
 * JPEG-LS encoder and decoder
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

#include "golomb.h"

/**
 * @file jpeg_ls.c
 * JPEG-LS encoder and decoder.
 */

typedef struct JpeglsContext{
    AVCodecContext *avctx;
    AVFrame picture;
}JpeglsContext;

typedef struct JLSState{
    int T1, T2, T3;
    int A[367], B[367], C[365], N[367];
    int limit, reset, bpp, qbpp, maxval, range;
    int near, twonear;
    int run_index[3];
}JLSState;

static const uint8_t log2_run[32]={
 0, 0, 0, 0, 1, 1, 1, 1,
 2, 2, 2, 2, 3, 3, 3, 3,
 4, 4, 5, 5, 6, 6, 7, 7,
 8, 9,10,11,12,13,14,15
};

/*
* Uncomment this to significantly speed up decoding of broken JPEG-LS
* (or test broken JPEG-LS decoder) and slow down ordinary decoding a bit.
*
* There is no Golomb code with length >= 32 bits possible, so check and
* avoid situation of 32 zeros, FFmpeg Golomb decoder is painfully slow
* on this errors.
*/
//#define JLS_BROKEN

/********** Functions for both encoder and decoder **********/

/**
 * Calculate initial JPEG-LS parameters
 */
static void ls_init_state(JLSState *state){
    int i;

    state->twonear = state->near * 2 + 1;
    state->range = ((state->maxval + state->twonear - 1) / state->twonear) + 1;

    // QBPP = ceil(log2(RANGE))
    for(state->qbpp = 0; (1 << state->qbpp) < state->range; state->qbpp++);

    if(state->bpp < 8)
        state->limit = 16 + 2 * state->bpp - state->qbpp;
    else
        state->limit = (4 * state->bpp) - state->qbpp;

    for(i = 0; i < 367; i++) {
        state->A[i] = FFMAX((state->range + 32) >> 6, 2);
        state->N[i] = 1;
    }

}

/**
 * Calculate quantized gradient value, used for context determination
 */
static inline int quantize(JLSState *s, int v){ //FIXME optimize
    if(v==0) return 0;
    if(v < 0){
        if(v <= -s->T3) return -4;
        if(v <= -s->T2) return -3;
        if(v <= -s->T1) return -2;
        if(v <  -s->near) return -1;
        return 0;
    }else{
        if(v <= s->near) return 0;
        if(v <  s->T1) return 1;
        if(v <  s->T2) return 2;
        if(v <  s->T3) return 3;
        return 4;
    }
}

/**
 * Custom value clipping function used in T1, T2, T3 calculation
 */
static inline int iso_clip(int v, int vmin, int vmax){
    if(v > vmax || v < vmin) return vmin;
    else                     return v;
}

/**
 * Calculate JPEG-LS codec values
 */
static void reset_ls_coding_parameters(JLSState *s, int reset_all){
    const int basic_t1= 3;
    const int basic_t2= 7;
    const int basic_t3= 21;
    int factor;

    if(s->maxval==0 || reset_all) s->maxval= (1 << s->bpp) - 1;

    if(s->maxval >=128){
        factor= (FFMIN(s->maxval, 4095) + 128)>>8;

        if(s->T1==0     || reset_all)
            s->T1= iso_clip(factor*(basic_t1-2) + 2 + 3*s->near, s->near+1, s->maxval);
        if(s->T2==0     || reset_all)
            s->T2= iso_clip(factor*(basic_t2-3) + 3 + 5*s->near, s->T1, s->maxval);
        if(s->T3==0     || reset_all)
            s->T3= iso_clip(factor*(basic_t3-4) + 4 + 7*s->near, s->T2, s->maxval);
    }else{
        factor= 256 / (s->maxval + 1);

        if(s->T1==0     || reset_all)
            s->T1= iso_clip(FFMAX(2, basic_t1/factor + 3*s->near), s->near+1, s->maxval);
        if(s->T2==0     || reset_all)
            s->T2= iso_clip(FFMAX(3, basic_t2/factor + 5*s->near), s->T1, s->maxval);
        if(s->T3==0     || reset_all)
            s->T3= iso_clip(FFMAX(4, basic_t3/factor + 6*s->near), s->T2, s->maxval);
    }

    if(s->reset==0  || reset_all) s->reset= 64;
//    av_log(NULL, AV_LOG_DEBUG, "[JPEG-LS RESET] T=%i,%i,%i\n", s->T1, s->T2, s->T3);
}


/********** Decoder-specific functions **********/

/**
 * Decode LSE block with initialization parameters
 */
static int decode_lse(MJpegDecodeContext *s)
{
    int len, id;

    /* XXX: verify len field validity */
    len = get_bits(&s->gb, 16);
    id = get_bits(&s->gb, 8);

    switch(id){
    case 1:
        s->maxval= get_bits(&s->gb, 16);
        s->t1= get_bits(&s->gb, 16);
        s->t2= get_bits(&s->gb, 16);
        s->t3= get_bits(&s->gb, 16);
        s->reset= get_bits(&s->gb, 16);

//        reset_ls_coding_parameters(s, 0);
        //FIXME quant table?
        break;
    case 2:
    case 3:
        av_log(s->avctx, AV_LOG_ERROR, "palette not supported\n");
        return -1;
    case 4:
        av_log(s->avctx, AV_LOG_ERROR, "oversize image not supported\n");
        return -1;
    default:
        av_log(s->avctx, AV_LOG_ERROR, "invalid id %d\n", id);
        return -1;
    }
//    av_log(s->avctx, AV_LOG_DEBUG, "ID=%i, T=%i,%i,%i\n", id, s->t1, s->t2, s->t3);

    return 0;
}

static void inline downscale_state(JLSState *state, int Q){
    if(state->N[Q] == state->reset){
        state->A[Q] >>=1;
        state->B[Q] >>=1;
        state->N[Q] >>=1;
    }
    state->N[Q]++;
}

static inline int update_state_regular(JLSState *state, int Q, int err){
    state->A[Q] += FFABS(err);
    err *= state->twonear;
    state->B[Q] += err;

    downscale_state(state, Q);

    if(state->B[Q] <= -state->N[Q]) {
        state->B[Q]= FFMAX(state->B[Q] + state->N[Q], 1-state->N[Q]);
        if(state->C[Q] > -128)
            state->C[Q]--;
    }else if(state->B[Q] > 0){
        state->B[Q]= FFMIN(state->B[Q] - state->N[Q], 0);
        if(state->C[Q] < 127)
            state->C[Q]++;
    }

    return err;
}

/**
 * Get context-dependent Golomb code, decode it and update context
 */
static inline int ls_get_code_regular(GetBitContext *gb, JLSState *state, int Q){
    int k, ret;

    for(k = 0; (state->N[Q] << k) < state->A[Q]; k++);

#ifdef JLS_BROKEN
    if(!show_bits_long(gb, 32))return -1;
#endif
    ret = get_ur_golomb_jpegls(gb, k, state->limit, state->qbpp);

    /* decode mapped error */
    if(ret & 1)
        ret = -((ret + 1) >> 1);
    else
        ret >>= 1;

    /* for NEAR=0, k=0 and 2*B[Q] <= - N[Q] mapping is reversed */
    if(!state->near && !k && (2 * state->B[Q] <= -state->N[Q]))
        ret = -(ret + 1);

    ret= update_state_regular(state, Q, ret);

    return ret;
}

/**
 * Get Golomb code, decode it and update state for run termination
 */
static inline int ls_get_code_runterm(GetBitContext *gb, JLSState *state, int RItype, int limit_add){
    int k, ret, temp, map;
    int Q = 365 + RItype;

    temp=  state->A[Q];
    if(RItype)
        temp += state->N[Q] >> 1;

    for(k = 0; (state->N[Q] << k) < temp; k++);

#ifdef JLS_BROKEN
    if(!show_bits_long(gb, 32))return -1;
#endif
    ret = get_ur_golomb_jpegls(gb, k, state->limit - limit_add - 1, state->qbpp);

    /* decode mapped error */
    map = 0;
    if(!k && (RItype || ret) && (2 * state->B[Q] < state->N[Q]))
        map = 1;
    ret += RItype + map;

    if(ret & 1){
        ret = map - ((ret + 1) >> 1);
        state->B[Q]++;
    } else {
        ret = ret >> 1;
    }

    /* update state */
    state->A[Q] += FFABS(ret) - RItype;
    ret *= state->twonear;
    downscale_state(state, Q);

    return ret;
}

#define R(a, i   ) (bits == 8 ?  ((uint8_t*)(a))[i]    :  ((uint16_t*)(a))[i]  )
#define W(a, i, v) (bits == 8 ? (((uint8_t*)(a))[i]=v) : (((uint16_t*)(a))[i]=v))
/**
 * Decode one line of image
 */
static inline void ls_decode_line(JLSState *state, MJpegDecodeContext *s, void *last, void *dst, int last2, int w, int stride, int comp, int bits){
    int i, x = 0;
    int Ra, Rb, Rc, Rd;
    int D0, D1, D2;

    while(x < w) {
        int err, pred;

        /* compute gradients */
        Ra = x ? R(dst, x - stride) : R(last, x);
        Rb = R(last, x);
        Rc = x ? R(last, x - stride) : last2;
        Rd = (x >= w - stride) ? R(last, x) : R(last, x + stride);
        D0 = Rd - Rb;
        D1 = Rb - Rc;
        D2 = Rc - Ra;
        /* run mode */
        if((FFABS(D0) <= state->near) && (FFABS(D1) <= state->near) && (FFABS(D2) <= state->near)) {
            int r;
            int RItype;

            /* decode full runs while available */
            while(get_bits1(&s->gb)) {
                int r;
                r = 1 << log2_run[state->run_index[comp]];
                if(x + r * stride > w) {
                    r = (w - x) / stride;
                }
                for(i = 0; i < r; i++) {
                    W(dst, x, Ra);
                    x += stride;
                }
                /* if EOL reached, we stop decoding */
                if(r != (1 << log2_run[state->run_index[comp]]))
                    return;
                if(state->run_index[comp] < 31)
                    state->run_index[comp]++;
                if(x + stride > w)
                    return;
            }
            /* decode aborted run */
            r = log2_run[state->run_index[comp]];
            if(r)
                r = get_bits_long(&s->gb, r);
            for(i = 0; i < r; i++) {
                W(dst, x, Ra);
                x += stride;
            }

            /* decode run termination value */
            Rb = R(last, x);
            RItype = (FFABS(Ra - Rb) <= state->near) ? 1 : 0;
            err = ls_get_code_runterm(&s->gb, state, RItype, log2_run[state->run_index[comp]]);
            if(state->run_index[comp])
                state->run_index[comp]--;

            if(state->near && RItype){
                pred = Ra + err;
            } else {
                if(Rb < Ra)
                    pred = Rb - err;
                else
                    pred = Rb + err;
            }
        } else { /* regular mode */
            int context, sign;

            context = quantize(state, D0) * 81 + quantize(state, D1) * 9 + quantize(state, D2);
            pred = mid_pred(Ra, Ra + Rb - Rc, Rb);

            if(context < 0){
                context = -context;
                sign = 1;
            }else{
                sign = 0;
            }

            if(sign){
                pred = av_clip(pred - state->C[context], 0, state->maxval);
                err = -ls_get_code_regular(&s->gb, state, context);
            } else {
                pred = av_clip(pred + state->C[context], 0, state->maxval);
                err = ls_get_code_regular(&s->gb, state, context);
            }

            /* we have to do something more for near-lossless coding */
            pred += err;
        }
        if(state->near){
            if(pred < -state->near)
                pred += state->range * state->twonear;
            else if(pred > state->maxval + state->near)
                pred -= state->range * state->twonear;
            pred = av_clip(pred, 0, state->maxval);
        }

        pred &= state->maxval;
        W(dst, x, pred);
        x += stride;
    }
}

static int ls_decode_picture(MJpegDecodeContext *s, int near, int point_transform, int ilv){
    int i, t = 0;
    uint8_t *zero, *last, *cur;
    JLSState *state;
    int off = 0, stride = 1, width, shift;

    zero = av_mallocz(s->picture.linesize[0]);
    last = zero;
    cur = s->picture.data[0];

    state = av_mallocz(sizeof(JLSState));
    /* initialize JPEG-LS state from JPEG parameters */
    state->near = near;
    state->bpp = (s->bits < 2) ? 2 : s->bits;
    state->maxval = s->maxval;
    state->T1 = s->t1;
    state->T2 = s->t2;
    state->T3 = s->t3;
    state->reset = s->reset;
    reset_ls_coding_parameters(state, 0);
    ls_init_state(state);

    if(s->bits <= 8)
        shift = point_transform + (8 - s->bits);
    else
        shift = point_transform + (16 - s->bits);

//    av_log(s->avctx, AV_LOG_DEBUG, "JPEG-LS params: %ix%i NEAR=%i MV=%i T(%i,%i,%i) RESET=%i, LIMIT=%i, qbpp=%i, RANGE=%i\n",s->width,s->height,state->near,state->maxval,state->T1,state->T2,state->T3,state->reset,state->limit,state->qbpp, state->range);
//    av_log(s->avctx, AV_LOG_DEBUG, "JPEG params: ILV=%i Pt=%i BPP=%i, scan = %i\n", ilv, point_transform, s->bits, s->cur_scan);
    if(ilv == 0) { /* separate planes */
        off = s->cur_scan - 1;
        stride = (s->nb_components > 1) ? 3 : 1;
        width = s->width * stride;
        cur += off;
        for(i = 0; i < s->height; i++) {
            if(s->bits <= 8){
                ls_decode_line(state, s, last, cur, t, width, stride, off,  8);
                t = last[0];
            }else{
                ls_decode_line(state, s, last, cur, t, width, stride, off, 16);
                t = *((uint16_t*)last);
            }
            last = cur;
            cur += s->picture.linesize[0];

            if (s->restart_interval && !--s->restart_count) {
                align_get_bits(&s->gb);
                skip_bits(&s->gb, 16); /* skip RSTn */
            }
        }
    } else if(ilv == 1) { /* line interleaving */
        int j;
        int Rc[3] = {0, 0, 0};
        memset(cur, 0, s->picture.linesize[0]);
        width = s->width * 3;
        for(i = 0; i < s->height; i++) {
            for(j = 0; j < 3; j++) {
                ls_decode_line(state, s, last + j, cur + j, Rc[j], width, 3, j, 8);
                Rc[j] = last[j];

                if (s->restart_interval && !--s->restart_count) {
                    align_get_bits(&s->gb);
                    skip_bits(&s->gb, 16); /* skip RSTn */
                }
            }
            last = cur;
            cur += s->picture.linesize[0];
        }
    } else if(ilv == 2) { /* sample interleaving */
        av_log(s->avctx, AV_LOG_ERROR, "Sample interleaved images are not supported.\n");
        av_free(state);
        av_free(zero);
        return -1;
    }

    if(shift){ /* we need to do point transform or normalize samples */
        int x, w;

        w = s->width * s->nb_components;

        if(s->bits <= 8){
            uint8_t *src = s->picture.data[0];

            for(i = 0; i < s->height; i++){
                for(x = off; x < w; x+= stride){
                    src[x] <<= shift;
                }
                src += s->picture.linesize[0];
            }
        }else{
            uint16_t *src = (uint16_t*) s->picture.data[0];

            for(i = 0; i < s->height; i++){
                for(x = 0; x < w; x++){
                    src[x] <<= shift;
                }
                src += s->picture.linesize[0]/2;
            }
        }
    }
    av_free(state);
    av_free(zero);

    return 0;
}

#if defined(CONFIG_ENCODERS) && defined(CONFIG_JPEGLS_ENCODER)
/********** Encoder-specific functions **********/

/**
 * Encode error from regular symbol
 */
static inline void ls_encode_regular(JLSState *state, PutBitContext *pb, int Q, int err){
    int k;
    int val;
    int map;

    for(k = 0; (state->N[Q] << k) < state->A[Q]; k++);

    map = !state->near && !k && (2 * state->B[Q] <= -state->N[Q]);

    if(err < 0)
        err += state->range;
    if(err >= ((state->range + 1) >> 1)) {
        err -= state->range;
        val = 2 * FFABS(err) - 1 - map;
    } else
        val = 2 * err + map;

    set_ur_golomb_jpegls(pb, val, k, state->limit, state->qbpp);

    update_state_regular(state, Q, err);
}

/**
 * Encode error from run termination
 */
static inline void ls_encode_runterm(JLSState *state, PutBitContext *pb, int RItype, int err, int limit_add){
    int k;
    int val, map;
    int Q = 365 + RItype;
    int temp;

    temp = state->A[Q];
    if(RItype)
        temp += state->N[Q] >> 1;
    for(k = 0; (state->N[Q] << k) < temp; k++);
    map = 0;
    if(!k && err && (2 * state->B[Q] < state->N[Q]))
        map = 1;

    if(err < 0)
        val = - (2 * err) - 1 - RItype + map;
    else
        val = 2 * err - RItype - map;
    set_ur_golomb_jpegls(pb, val, k, state->limit - limit_add - 1, state->qbpp);

    if(err < 0)
        state->B[Q]++;
    state->A[Q] += (val + 1 - RItype) >> 1;

    downscale_state(state, Q);
}

/**
 * Encode run value as specified by JPEG-LS standard
 */
static inline void ls_encode_run(JLSState *state, PutBitContext *pb, int run, int comp, int trail){
    while(run >= (1 << log2_run[state->run_index[comp]])){
        put_bits(pb, 1, 1);
        run -= 1 << log2_run[state->run_index[comp]];
        if(state->run_index[comp] < 31)
            state->run_index[comp]++;
    }
    /* if hit EOL, encode another full run, else encode aborted run */
    if(!trail && run) {
        put_bits(pb, 1, 1);
    }else if(trail){
        put_bits(pb, 1, 0);
        if(log2_run[state->run_index[comp]])
            put_bits(pb, log2_run[state->run_index[comp]], run);
    }
}

/**
 * Encode one line of image
 */
static inline void ls_encode_line(JLSState *state, PutBitContext *pb, void *last, void *cur, int last2, int w, int stride, int comp, int bits){
    int x = 0;
    int Ra, Rb, Rc, Rd;
    int D0, D1, D2;

    while(x < w) {
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
        if((FFABS(D0) <= state->near) && (FFABS(D1) <= state->near) && (FFABS(D2) <= state->near)) {
            int RUNval, RItype, run;

            run = 0;
            RUNval = Ra;
            while(x < w && (FFABS(R(cur, x) - RUNval) <= state->near)){
                run++;
                W(cur, x, Ra);
                x += stride;
            }
            ls_encode_run(state, pb, run, comp, x < w);
            if(x >= w)
                return;
            Rb = R(last, x);
            RItype = (FFABS(Ra - Rb) <= state->near);
            pred = RItype ? Ra : Rb;
            err = R(cur, x) - pred;

            if(!RItype && Ra > Rb)
                err = -err;

            if(state->near){
                if(err > 0)
                    err = (state->near + err) / state->twonear;
                else
                    err = -(state->near - err) / state->twonear;

                if(RItype || (Rb >= Ra))
                    Ra = av_clip(pred + err * state->twonear, 0, state->maxval);
                else
                    Ra = av_clip(pred - err * state->twonear, 0, state->maxval);
                W(cur, x, Ra);
            }
            if(err < 0)
                err += state->range;
            if(err >= ((state->range + 1) >> 1))
                err -= state->range;

            ls_encode_runterm(state, pb, RItype, err, log2_run[state->run_index[comp]]);

            if(state->run_index[comp] > 0)
                state->run_index[comp]--;
        } else { /* regular mode */
            int context;

            context = quantize(state, D0) * 81 + quantize(state, D1) * 9 + quantize(state, D2);
            pred = mid_pred(Ra, Ra + Rb - Rc, Rb);

            if(context < 0){
                context = -context;
                sign = 1;
                pred = av_clip(pred - state->C[context], 0, state->maxval);
                err = pred - R(cur, x);
            }else{
                sign = 0;
                pred = av_clip(pred + state->C[context], 0, state->maxval);
                err = R(cur, x) - pred;
            }

            if(state->near){
                if(err > 0)
                    err = (state->near + err) / state->twonear;
                else
                    err = -(state->near - err) / state->twonear;
                if(!sign)
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

static void ls_store_lse(JLSState *state, PutBitContext *pb){
    /* Test if we have default params and don't need to store LSE */
    JLSState state2;
    memset(&state2, 0, sizeof(JLSState));
    state2.bpp = state->bpp;
    state2.near = state->near;
    reset_ls_coding_parameters(&state2, 1);
    if(state->T1 == state2.T1 && state->T2 == state2.T2 && state->T3 == state2.T3 && state->reset == state2.reset)
        return;
    /* store LSE type 1 */
    put_marker(pb, LSE);
    put_bits(pb, 16, 13);
    put_bits(pb, 8,   1);
    put_bits(pb, 16, state->maxval);
    put_bits(pb, 16, state->T1);
    put_bits(pb, 16, state->T2);
    put_bits(pb, 16, state->T3);
    put_bits(pb, 16, state->reset);
}

static int encode_picture_ls(AVCodecContext *avctx, unsigned char *buf, int buf_size, void *data){
    JpeglsContext * const s = avctx->priv_data;
    AVFrame *pict = data;
    AVFrame * const p= (AVFrame*)&s->picture;
    const int near = avctx->prediction_method;
    PutBitContext pb, pb2;
    GetBitContext gb;
    uint8_t *buf2, *zero, *cur, *last;
    JLSState *state;
    int i, size;
    int comps;

    buf2 = av_malloc(buf_size);

    init_put_bits(&pb, buf, buf_size);
    init_put_bits(&pb2, buf2, buf_size);

    *p = *pict;
    p->pict_type= FF_I_TYPE;
    p->key_frame= 1;

    if(avctx->pix_fmt == PIX_FMT_GRAY8 || avctx->pix_fmt == PIX_FMT_GRAY16)
        comps = 1;
    else
        comps = 3;

    /* write our own JPEG header, can't use mjpeg_picture_header */
    put_marker(&pb, SOI);
    put_marker(&pb, SOF48);
    put_bits(&pb, 16, 8 + comps * 3); // header size depends on components
    put_bits(&pb,  8, (avctx->pix_fmt == PIX_FMT_GRAY16) ? 16 : 8); // bpp
    put_bits(&pb, 16, avctx->height);
    put_bits(&pb, 16, avctx->width);
    put_bits(&pb,  8, comps);         // components
    for(i = 1; i <= comps; i++) {
        put_bits(&pb,  8, i);    // component ID
        put_bits(&pb,  8, 0x11); // subsampling: none
        put_bits(&pb,  8, 0);    // Tiq, used by JPEG-LS ext
    }

    put_marker(&pb, SOS);
    put_bits(&pb, 16, 6 + comps * 2);
    put_bits(&pb,  8, comps);
    for(i = 1; i <= comps; i++) {
        put_bits(&pb,  8, i);  // component ID
        put_bits(&pb,  8, 0);  // mapping index: none
    }
    put_bits(&pb,  8, near);
    put_bits(&pb,  8, (comps > 1) ? 1 : 0); // interleaving: 0 - plane, 1 - line
    put_bits(&pb,  8, 0); // point transform: none

    state = av_mallocz(sizeof(JLSState));
    /* initialize JPEG-LS state from JPEG parameters */
    state->near = near;
    state->bpp = (avctx->pix_fmt == PIX_FMT_GRAY16) ? 16 : 8;
    reset_ls_coding_parameters(state, 0);
    ls_init_state(state);

    ls_store_lse(state, &pb);

    zero = av_mallocz(p->linesize[0]);
    last = zero;
    cur = p->data[0];
    if(avctx->pix_fmt == PIX_FMT_GRAY8){
        int t = 0;

        for(i = 0; i < avctx->height; i++) {
            ls_encode_line(state, &pb2, last, cur, t, avctx->width, 1, 0,  8);
            t = last[0];
            last = cur;
            cur += p->linesize[0];
        }
    }else if(avctx->pix_fmt == PIX_FMT_GRAY16){
        int t = 0;

        for(i = 0; i < avctx->height; i++) {
            ls_encode_line(state, &pb2, last, cur, t, avctx->width, 1, 0, 16);
            t = *((uint16_t*)last);
            last = cur;
            cur += p->linesize[0];
        }
    }else if(avctx->pix_fmt == PIX_FMT_RGB24){
        int j, width;
        int Rc[3] = {0, 0, 0};

        width = avctx->width * 3;
        for(i = 0; i < avctx->height; i++) {
            for(j = 0; j < 3; j++) {
                ls_encode_line(state, &pb2, last + j, cur + j, Rc[j], width, 3, j, 8);
                Rc[j] = last[j];
            }
            last = cur;
            cur += s->picture.linesize[0];
        }
    }else if(avctx->pix_fmt == PIX_FMT_BGR24){
        int j, width;
        int Rc[3] = {0, 0, 0};

        width = avctx->width * 3;
        for(i = 0; i < avctx->height; i++) {
            for(j = 2; j >= 0; j--) {
                ls_encode_line(state, &pb2, last + j, cur + j, Rc[j], width, 3, j, 8);
                Rc[j] = last[j];
            }
            last = cur;
            cur += s->picture.linesize[0];
        }
    }

    av_free(zero);
    av_free(state);

    // the specification says that after doing 0xff escaping unused bits in the
    // last byte must be set to 0, so just append 7 "optional" zero-bits to
    // avoid special-casing.
    put_bits(&pb2, 7, 0);
    size = put_bits_count(&pb2);
    flush_put_bits(&pb2);
    /* do escape coding */
    init_get_bits(&gb, buf2, size);
    size -= 7;
    while(get_bits_count(&gb) < size){
        int v;
        v = get_bits(&gb, 8);
        put_bits(&pb, 8, v);
        if(v == 0xFF){
            v = get_bits(&gb, 7);
            put_bits(&pb, 8, v);
        }
    }
    align_put_bits(&pb);
    av_free(buf2);

    /* End of image */
    put_marker(&pb, EOI);
    flush_put_bits(&pb);

    emms_c();

    return put_bits_count(&pb) >> 3;
}

static int encode_init_ls(AVCodecContext *ctx) {
    JpeglsContext *c = (JpeglsContext*)ctx->priv_data;

    c->avctx = ctx;
    ctx->coded_frame = &c->picture;

    if(ctx->pix_fmt != PIX_FMT_GRAY8 && ctx->pix_fmt != PIX_FMT_GRAY16 && ctx->pix_fmt != PIX_FMT_RGB24 && ctx->pix_fmt != PIX_FMT_BGR24){
        av_log(ctx, AV_LOG_ERROR, "Only grayscale and RGB24/BGR24 images are supported\n");
        return -1;
    }
    return 0;
}

AVCodec jpegls_encoder = { //FIXME avoid MPV_* lossless jpeg shouldnt need them
    "jpegls",
    CODEC_TYPE_VIDEO,
    CODEC_ID_JPEGLS,
    sizeof(JpeglsContext),
    encode_init_ls,
    encode_picture_ls,
    NULL,
    .pix_fmts= (enum PixelFormat[]){PIX_FMT_BGR24, PIX_FMT_RGB24, PIX_FMT_GRAY8, PIX_FMT_GRAY16, -1},
};
#endif
