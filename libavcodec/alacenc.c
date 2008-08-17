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

    int interlacing_shift;
    int interlacing_leftweight;
    PutBitContext pbctx;
    DSPContext dspctx;
    AVCodecContext *avctx;
} AlacEncodeContext;


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
    put_bits(&s->pbctx, 3,  s->channels-1);                 // No. of channels -1
    put_bits(&s->pbctx, 16, 0);                             // Seems to be zero
    put_bits(&s->pbctx, 1,  1);                             // Sample count is in the header
    put_bits(&s->pbctx, 2,  0);                             // FIXME: Wasted bytes field
    put_bits(&s->pbctx, 1,  is_verbatim);                   // Audio block is verbatim
    put_bits(&s->pbctx, 32, s->avctx->frame_size);          // No. of samples in the frame
}

static void write_compressed_frame(AlacEncodeContext *s)
{
    int i, j;

    /* only simple mid/side decorrelation supported as of now */
    alac_stereo_decorrelation(s);
    put_bits(&s->pbctx, 8, s->interlacing_shift);
    put_bits(&s->pbctx, 8, s->interlacing_leftweight);

    for(i=0;i<s->channels;i++) {

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

    for(i=0;i<s->channels;i++) {
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
    s->channels            = avctx->channels;
    s->samplerate          = avctx->sample_rate;

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
                               avctx->frame_size*s->channels*avctx->bits_per_sample)>>3;

    s->write_sample_size  = avctx->bits_per_sample + s->channels - 1; // FIXME: consider wasted_bytes

    AV_WB32(alac_extradata,    ALAC_EXTRADATA_SIZE);
    AV_WB32(alac_extradata+4,  MKBETAG('a','l','a','c'));
    AV_WB32(alac_extradata+12, avctx->frame_size);
    AV_WB8 (alac_extradata+17, avctx->bits_per_sample);
    AV_WB8 (alac_extradata+21, s->channels);
    AV_WB32(alac_extradata+24, s->max_coded_frame_size);
    AV_WB32(alac_extradata+28, s->samplerate*s->channels*avctx->bits_per_sample); // average bitrate
    AV_WB32(alac_extradata+32, s->samplerate);

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

    allocate_sample_buffers(s);

    return 0;
}

static av_cold int alac_encode_close(AVCodecContext *avctx)
{
    AlacEncodeContext *s = avctx->priv_data;

    av_freep(&avctx->extradata);
    avctx->extradata_size = 0;
    av_freep(&avctx->coded_frame);
    free_sample_buffers(s);
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
