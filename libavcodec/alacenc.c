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
#include "bitstream.h"
#include "dsputil.h"
#include "lpc.h"

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
    int max_coded_frame_size;
    int write_sample_size;
    int32_t sample_buf[MAX_CHANNELS][DEFAULT_FRAME_SIZE];
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
    put_bits(&s->pbctx, 32, s->avctx->frame_size);          // No. of samples in the frame
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

static void write_compressed_frame(AlacEncodeContext *s)
{
    int i, j;

    /* only simple mid/side decorrelation supported as of now */
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
    avctx->bits_per_sample = DEFAULT_SAMPLE_SIZE;

    if(avctx->sample_fmt != SAMPLE_FMT_S16) {
        av_log(avctx, AV_LOG_ERROR, "only pcm_s16 input samples are supported\n");
        return -1;
    }

    // Set default compression level
    if(avctx->compression_level == FF_COMPRESSION_DEFAULT)
        s->compression_level = 1;
    else
        s->compression_level = av_clip(avctx->compression_level, 0, 1);

    // Initialize default Rice parameters
    s->rc.history_mult    = 40;
    s->rc.initial_history = 10;
    s->rc.k_modifier      = 14;
    s->rc.rice_modifier   = 4;

    s->max_coded_frame_size = (ALAC_FRAME_HEADER_SIZE + ALAC_FRAME_FOOTER_SIZE +
                               avctx->frame_size*avctx->channels*avctx->bits_per_sample)>>3;

    s->write_sample_size  = avctx->bits_per_sample + avctx->channels - 1; // FIXME: consider wasted_bytes

    AV_WB32(alac_extradata,    ALAC_EXTRADATA_SIZE);
    AV_WB32(alac_extradata+4,  MKBETAG('a','l','a','c'));
    AV_WB32(alac_extradata+12, avctx->frame_size);
    AV_WB8 (alac_extradata+17, avctx->bits_per_sample);
    AV_WB8 (alac_extradata+21, avctx->channels);
    AV_WB32(alac_extradata+24, s->max_coded_frame_size);
    AV_WB32(alac_extradata+28, avctx->sample_rate*avctx->channels*avctx->bits_per_sample); // average bitrate
    AV_WB32(alac_extradata+32, avctx->sample_rate);

    // Set relevant extradata fields
    if(s->compression_level > 0) {
        AV_WB8(alac_extradata+18, s->rc.history_mult);
        AV_WB8(alac_extradata+19, s->rc.initial_history);
        AV_WB8(alac_extradata+20, s->rc.k_modifier);
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
    CODEC_TYPE_AUDIO,
    CODEC_ID_ALAC,
    sizeof(AlacEncodeContext),
    alac_encode_init,
    alac_encode_frame,
    alac_encode_close,
    .capabilities = CODEC_CAP_SMALL_LAST_FRAME,
    .long_name = NULL_IF_CONFIG_SMALL("ALAC (Apple Lossless Audio Codec)"),
};
