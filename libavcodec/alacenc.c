/**
 * ALAC audio encoder
 * Copyright (c) 2008  Jaikrishnan Menon <realityman@gmx.net>
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

#include "avcodec.h"
#include "put_bits.h"
#include "dsputil.h"
#include "lpc.h"
#include "mathops.h"

#define DEFAULT_FRAME_SIZE        4096
#define DEFAULT_SAMPLE_SIZE       16
#define MAX_CHANNELS              8
#define ALAC_EXTRADATA_SIZE       36
#define ALAC_FRAME_HEADER_SIZE    55
#define ALAC_FRAME_FOOTER_SIZE    3

#define ALAC_ESCAPE_CODE          0x1FF
#define ALAC_MAX_LPC_ORDER        30
#define DEFAULT_MAX_PRED_ORDER    6
#define DEFAULT_MIN_PRED_ORDER    4
#define ALAC_MAX_LPC_PRECISION    9
#define ALAC_MAX_LPC_SHIFT        9

#define ALAC_CHMODE_LEFT_RIGHT    0
#define ALAC_CHMODE_LEFT_SIDE     1
#define ALAC_CHMODE_RIGHT_SIDE    2
#define ALAC_CHMODE_MID_SIDE      3

typedef struct RiceContext {
    int history_mult;
    int initial_history;
    int k_modifier;
    int rice_modifier;
} RiceContext;

typedef struct LPCContext {
    int lpc_order;
    int lpc_coeff[ALAC_MAX_LPC_ORDER+1];
    int lpc_quant;
} LPCContext;

typedef struct AlacEncodeContext {
    int compression_level;
    int min_prediction_order;
    int max_prediction_order;
    int max_coded_frame_size;
    int write_sample_size;
    int32_t sample_buf[MAX_CHANNELS][DEFAULT_FRAME_SIZE];
    int32_t predictor_buf[DEFAULT_FRAME_SIZE];
    int interlacing_shift;
    int interlacing_leftweight;
    PutBitContext pbctx;
    RiceContext rc;
    LPCContext lpc[MAX_CHANNELS];
    DSPContext dspctx;
    AVCodecContext *avctx;
} AlacEncodeContext;


static void init_sample_buffers(AlacEncodeContext *s, int16_t *input_samples)
{
    int ch, i;

    for(ch=0;ch<s->avctx->channels;ch++) {
        int16_t *sptr = input_samples + ch;
        for(i=0;i<s->avctx->frame_size;i++) {
            s->sample_buf[ch][i] = *sptr;
            sptr += s->avctx->channels;
        }
    }
}

static void encode_scalar(AlacEncodeContext *s, int x, int k, int write_sample_size)
{
    int divisor, q, r;

    k = FFMIN(k, s->rc.k_modifier);
    divisor = (1<<k) - 1;
    q = x / divisor;
    r = x % divisor;

    if(q > 8) {
        // write escape code and sample value directly
        put_bits(&s->pbctx, 9, ALAC_ESCAPE_CODE);
        put_bits(&s->pbctx, write_sample_size, x);
    } else {
        if(q)
            put_bits(&s->pbctx, q, (1<<q) - 1);
        put_bits(&s->pbctx, 1, 0);

        if(k != 1) {
            if(r > 0)
                put_bits(&s->pbctx, k, r+1);
            else
                put_bits(&s->pbctx, k-1, 0);
        }
    }
}

static void write_frame_header(AlacEncodeContext *s, int is_verbatim)
{
    put_bits(&s->pbctx, 3,  s->avctx->channels-1);          // No. of channels -1
    put_bits(&s->pbctx, 16, 0);                             // Seems to be zero
    put_bits(&s->pbctx, 1,  1);                             // Sample count is in the header
    put_bits(&s->pbctx, 2,  0);                             // FIXME: Wasted bytes field
    put_bits(&s->pbctx, 1,  is_verbatim);                   // Audio block is verbatim
    put_bits32(&s->pbctx, s->avctx->frame_size);            // No. of samples in the frame
}

static void calc_predictor_params(AlacEncodeContext *s, int ch)
{
    int32_t coefs[MAX_LPC_ORDER][MAX_LPC_ORDER];
    int shift[MAX_LPC_ORDER];
    int opt_order;

    if (s->compression_level == 1) {
        s->lpc[ch].lpc_order = 6;
        s->lpc[ch].lpc_quant = 6;
        s->lpc[ch].lpc_coeff[0] =  160;
        s->lpc[ch].lpc_coeff[1] = -190;
        s->lpc[ch].lpc_coeff[2] =  170;
        s->lpc[ch].lpc_coeff[3] = -130;
        s->lpc[ch].lpc_coeff[4] =   80;
        s->lpc[ch].lpc_coeff[5] =  -25;
    } else {
        opt_order = ff_lpc_calc_coefs(&s->dspctx, s->sample_buf[ch],
                                      s->avctx->frame_size,
                                      s->min_prediction_order,
                                      s->max_prediction_order,
                                      ALAC_MAX_LPC_PRECISION, coefs, shift, 1,
                                      ORDER_METHOD_EST, ALAC_MAX_LPC_SHIFT, 1);

        s->lpc[ch].lpc_order = opt_order;
        s->lpc[ch].lpc_quant = shift[opt_order-1];
        memcpy(s->lpc[ch].lpc_coeff, coefs[opt_order-1], opt_order*sizeof(int));
    }
}

static int estimate_stereo_mode(int32_t *left_ch, int32_t *right_ch, int n)
{
    int i, best;
    int32_t lt, rt;
    uint64_t sum[4];
    uint64_t score[4];

    /* calculate sum of 2nd order residual for each channel */
    sum[0] = sum[1] = sum[2] = sum[3] = 0;
    for(i=2; i<n; i++) {
        lt = left_ch[i] - 2*left_ch[i-1] + left_ch[i-2];
        rt = right_ch[i] - 2*right_ch[i-1] + right_ch[i-2];
        sum[2] += FFABS((lt + rt) >> 1);
        sum[3] += FFABS(lt - rt);
        sum[0] += FFABS(lt);
        sum[1] += FFABS(rt);
    }

    /* calculate score for each mode */
    score[0] = sum[0] + sum[1];
    score[1] = sum[0] + sum[3];
    score[2] = sum[1] + sum[3];
    score[3] = sum[2] + sum[3];

    /* return mode with lowest score */
    best = 0;
    for(i=1; i<4; i++) {
        if(score[i] < score[best]) {
            best = i;
        }
    }
    return best;
}

static void alac_stereo_decorrelation(AlacEncodeContext *s)
{
    int32_t *left = s->sample_buf[0], *right = s->sample_buf[1];
    int i, mode, n = s->avctx->frame_size;
    int32_t tmp;

    mode = estimate_stereo_mode(left, right, n);

    switch(mode)
    {
        case ALAC_CHMODE_LEFT_RIGHT:
            s->interlacing_leftweight = 0;
            s->interlacing_shift = 0;
            break;

        case ALAC_CHMODE_LEFT_SIDE:
            for(i=0; i<n; i++) {
                right[i] = left[i] - right[i];
            }
            s->interlacing_leftweight = 1;
            s->interlacing_shift = 0;
            break;

        case ALAC_CHMODE_RIGHT_SIDE:
            for(i=0; i<n; i++) {
                tmp = right[i];
                right[i] = left[i] - right[i];
                left[i] = tmp + (right[i] >> 31);
            }
            s->interlacing_leftweight = 1;
            s->interlacing_shift = 31;
            break;

        default:
            for(i=0; i<n; i++) {
                tmp = left[i];
                left[i] = (tmp + right[i]) >> 1;
                right[i] = tmp - right[i];
            }
            s->interlacing_leftweight = 1;
            s->interlacing_shift = 1;
            break;
    }
}

static void alac_linear_predictor(AlacEncodeContext *s, int ch)
{
    int i;
    LPCContext lpc = s->lpc[ch];

    if(lpc.lpc_order == 31) {
        s->predictor_buf[0] = s->sample_buf[ch][0];

        for(i=1; i<s->avctx->frame_size; i++)
            s->predictor_buf[i] = s->sample_buf[ch][i] - s->sample_buf[ch][i-1];

        return;
    }

    // generalised linear predictor

    if(lpc.lpc_order > 0) {
        int32_t *samples  = s->sample_buf[ch];
        int32_t *residual = s->predictor_buf;

        // generate warm-up samples
        residual[0] = samples[0];
        for(i=1;i<=lpc.lpc_order;i++)
            residual[i] = samples[i] - samples[i-1];

        // perform lpc on remaining samples
        for(i = lpc.lpc_order + 1; i < s->avctx->frame_size; i++) {
            int sum = 1 << (lpc.lpc_quant - 1), res_val, j;

            for (j = 0; j < lpc.lpc_order; j++) {
                sum += (samples[lpc.lpc_order-j] - samples[0]) *
                        lpc.lpc_coeff[j];
            }

            sum >>= lpc.lpc_quant;
            sum += samples[0];
            residual[i] = sign_extend(samples[lpc.lpc_order+1] - sum,
                                      s->write_sample_size);
            res_val = residual[i];

            if(res_val) {
                int index = lpc.lpc_order - 1;
                int neg = (res_val < 0);

                while(index >= 0 && (neg ? (res_val < 0):(res_val > 0))) {
                    int val = samples[0] - samples[lpc.lpc_order - index];
                    int sign = (val ? FFSIGN(val) : 0);

                    if(neg)
                        sign*=-1;

                    lpc.lpc_coeff[index] -= sign;
                    val *= sign;
                    res_val -= ((val >> lpc.lpc_quant) *
                            (lpc.lpc_order - index));
                    index--;
                }
            }
            samples++;
        }
    }
}

static void alac_entropy_coder(AlacEncodeContext *s)
{
    unsigned int history = s->rc.initial_history;
    int sign_modifier = 0, i, k;
    int32_t *samples = s->predictor_buf;

    for(i=0;i < s->avctx->frame_size;) {
        int x;

        k = av_log2((history >> 9) + 3);

        x = -2*(*samples)-1;
        x ^= (x>>31);

        samples++;
        i++;

        encode_scalar(s, x - sign_modifier, k, s->write_sample_size);

        history += x * s->rc.history_mult
                   - ((history * s->rc.history_mult) >> 9);

        sign_modifier = 0;
        if(x > 0xFFFF)
            history = 0xFFFF;

        if((history < 128) && (i < s->avctx->frame_size)) {
            unsigned int block_size = 0;

            k = 7 - av_log2(history) + ((history + 16) >> 6);

            while((*samples == 0) && (i < s->avctx->frame_size)) {
                samples++;
                i++;
                block_size++;
            }
            encode_scalar(s, block_size, k, 16);

            sign_modifier = (block_size <= 0xFFFF);

            history = 0;
        }

    }
}

static void write_compressed_frame(AlacEncodeContext *s)
{
    int i, j;

    if(s->avctx->channels == 2)
        alac_stereo_decorrelation(s);
    put_bits(&s->pbctx, 8, s->interlacing_shift);
    put_bits(&s->pbctx, 8, s->interlacing_leftweight);

    for(i=0;i<s->avctx->channels;i++) {

        calc_predictor_params(s, i);

        put_bits(&s->pbctx, 4, 0);  // prediction type : currently only type 0 has been RE'd
        put_bits(&s->pbctx, 4, s->lpc[i].lpc_quant);

        put_bits(&s->pbctx, 3, s->rc.rice_modifier);
        put_bits(&s->pbctx, 5, s->lpc[i].lpc_order);
        // predictor coeff. table
        for(j=0;j<s->lpc[i].lpc_order;j++) {
            put_sbits(&s->pbctx, 16, s->lpc[i].lpc_coeff[j]);
        }
    }

    // apply lpc and entropy coding to audio samples

    for(i=0;i<s->avctx->channels;i++) {
        alac_linear_predictor(s, i);
        alac_entropy_coder(s);
    }
}

static av_cold int alac_encode_init(AVCodecContext *avctx)
{
    AlacEncodeContext *s    = avctx->priv_data;
    uint8_t *alac_extradata = av_mallocz(ALAC_EXTRADATA_SIZE+1);

    avctx->frame_size      = DEFAULT_FRAME_SIZE;
    avctx->bits_per_coded_sample = DEFAULT_SAMPLE_SIZE;

    if(avctx->sample_fmt != SAMPLE_FMT_S16) {
        av_log(avctx, AV_LOG_ERROR, "only pcm_s16 input samples are supported\n");
        return -1;
    }

    // Set default compression level
    if(avctx->compression_level == FF_COMPRESSION_DEFAULT)
        s->compression_level = 2;
    else
        s->compression_level = av_clip(avctx->compression_level, 0, 2);

    // Initialize default Rice parameters
    s->rc.history_mult    = 40;
    s->rc.initial_history = 10;
    s->rc.k_modifier      = 14;
    s->rc.rice_modifier   = 4;

    s->max_coded_frame_size = 8 + (avctx->frame_size*avctx->channels*avctx->bits_per_coded_sample>>3);

    s->write_sample_size  = avctx->bits_per_coded_sample + avctx->channels - 1; // FIXME: consider wasted_bytes

    AV_WB32(alac_extradata,    ALAC_EXTRADATA_SIZE);
    AV_WB32(alac_extradata+4,  MKBETAG('a','l','a','c'));
    AV_WB32(alac_extradata+12, avctx->frame_size);
    AV_WB8 (alac_extradata+17, avctx->bits_per_coded_sample);
    AV_WB8 (alac_extradata+21, avctx->channels);
    AV_WB32(alac_extradata+24, s->max_coded_frame_size);
    AV_WB32(alac_extradata+28, avctx->sample_rate*avctx->channels*avctx->bits_per_coded_sample); // average bitrate
    AV_WB32(alac_extradata+32, avctx->sample_rate);

    // Set relevant extradata fields
    if(s->compression_level > 0) {
        AV_WB8(alac_extradata+18, s->rc.history_mult);
        AV_WB8(alac_extradata+19, s->rc.initial_history);
        AV_WB8(alac_extradata+20, s->rc.k_modifier);
    }

    s->min_prediction_order = DEFAULT_MIN_PRED_ORDER;
    if(avctx->min_prediction_order >= 0) {
        if(avctx->min_prediction_order < MIN_LPC_ORDER ||
           avctx->min_prediction_order > ALAC_MAX_LPC_ORDER) {
            av_log(avctx, AV_LOG_ERROR, "invalid min prediction order: %d\n", avctx->min_prediction_order);
                return -1;
        }

        s->min_prediction_order = avctx->min_prediction_order;
    }

    s->max_prediction_order = DEFAULT_MAX_PRED_ORDER;
    if(avctx->max_prediction_order >= 0) {
        if(avctx->max_prediction_order < MIN_LPC_ORDER ||
           avctx->max_prediction_order > ALAC_MAX_LPC_ORDER) {
            av_log(avctx, AV_LOG_ERROR, "invalid max prediction order: %d\n", avctx->max_prediction_order);
                return -1;
        }

        s->max_prediction_order = avctx->max_prediction_order;
    }

    if(s->max_prediction_order < s->min_prediction_order) {
        av_log(avctx, AV_LOG_ERROR, "invalid prediction orders: min=%d max=%d\n",
               s->min_prediction_order, s->max_prediction_order);
        return -1;
    }

    avctx->extradata = alac_extradata;
    avctx->extradata_size = ALAC_EXTRADATA_SIZE;

    avctx->coded_frame = avcodec_alloc_frame();
    avctx->coded_frame->key_frame = 1;

    s->avctx = avctx;
    dsputil_init(&s->dspctx, avctx);

    return 0;
}

static int alac_encode_frame(AVCodecContext *avctx, uint8_t *frame,
                             int buf_size, void *data)
{
    AlacEncodeContext *s = avctx->priv_data;
    PutBitContext *pb = &s->pbctx;
    int i, out_bytes, verbatim_flag = 0;

    if(avctx->frame_size > DEFAULT_FRAME_SIZE) {
        av_log(avctx, AV_LOG_ERROR, "input frame size exceeded\n");
        return -1;
    }

    if(buf_size < 2*s->max_coded_frame_size) {
        av_log(avctx, AV_LOG_ERROR, "buffer size is too small\n");
        return -1;
    }

verbatim:
    init_put_bits(pb, frame, buf_size);

    if((s->compression_level == 0) || verbatim_flag) {
        // Verbatim mode
        int16_t *samples = data;
        write_frame_header(s, 1);
        for(i=0; i<avctx->frame_size*avctx->channels; i++) {
            put_sbits(pb, 16, *samples++);
        }
    } else {
        init_sample_buffers(s, data);
        write_frame_header(s, 0);
        write_compressed_frame(s);
    }

    put_bits(pb, 3, 7);
    flush_put_bits(pb);
    out_bytes = put_bits_count(pb) >> 3;

    if(out_bytes > s->max_coded_frame_size) {
        /* frame too large. use verbatim mode */
        if(verbatim_flag || (s->compression_level == 0)) {
            /* still too large. must be an error. */
            av_log(avctx, AV_LOG_ERROR, "error encoding frame\n");
            return -1;
        }
        verbatim_flag = 1;
        goto verbatim;
    }

    return out_bytes;
}

static av_cold int alac_encode_close(AVCodecContext *avctx)
{
    av_freep(&avctx->extradata);
    avctx->extradata_size = 0;
    av_freep(&avctx->coded_frame);
    return 0;
}

AVCodec alac_encoder = {
    "alac",
    AVMEDIA_TYPE_AUDIO,
    CODEC_ID_ALAC,
    sizeof(AlacEncodeContext),
    alac_encode_init,
    alac_encode_frame,
    alac_encode_close,
    .capabilities = CODEC_CAP_SMALL_LAST_FRAME,
    .sample_fmts = (const enum SampleFormat[]){ SAMPLE_FMT_S16, SAMPLE_FMT_NONE},
    .long_name = NULL_IF_CONFIG_SMALL("ALAC (Apple Lossless Audio Codec)"),
};
