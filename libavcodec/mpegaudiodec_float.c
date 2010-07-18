/*
 * Float MPEG Audio decoder
 * Copyright (c) 2010 Michael Niedermayer
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

#define CONFIG_FLOAT 1
#include "mpegaudiodec.c"

void ff_mpa_synth_filter_float(MPADecodeContext *s, float *synth_buf_ptr,
                               int *synth_buf_offset,
                               float *window, int *dither_state,
                               float *samples, int incr,
                               float sb_samples[SBLIMIT])
{
    float *synth_buf;
    int offset;

    offset = *synth_buf_offset;
    synth_buf = synth_buf_ptr + offset;

    s->dct.dct32(synth_buf, sb_samples);
    s->apply_window_mp3(synth_buf, window, dither_state, samples, incr);

    offset = (offset - 32) & 511;
    *synth_buf_offset = offset;
}

static void compute_antialias_float(MPADecodeContext *s,
                              GranuleDef *g)
{
    float *ptr;
    int n, i;

    /* we antialias only "long" bands */
    if (g->block_type == 2) {
        if (!g->switch_point)
            return;
        /* XXX: check this for 8000Hz case */
        n = 1;
    } else {
        n = SBLIMIT - 1;
    }

    ptr = g->sb_hybrid + 18;
    for(i = n;i > 0;i--) {
        float tmp0, tmp1;
        float *csa = &csa_table_float[0][0];
#define FLOAT_AA(j)\
        tmp0= ptr[-1-j];\
        tmp1= ptr[   j];\
        ptr[-1-j] = tmp0 * csa[0+4*j] - tmp1 * csa[1+4*j];\
        ptr[   j] = tmp0 * csa[1+4*j] + tmp1 * csa[0+4*j];

        FLOAT_AA(0)
        FLOAT_AA(1)
        FLOAT_AA(2)
        FLOAT_AA(3)
        FLOAT_AA(4)
        FLOAT_AA(5)
        FLOAT_AA(6)
        FLOAT_AA(7)

        ptr += 18;
    }
}

static av_cold int decode_end(AVCodecContext * avctx)
{
    MPADecodeContext *s = avctx->priv_data;
    ff_dct_end(&s->dct);
    return 0;
}

#if CONFIG_MP1FLOAT_DECODER
AVCodec mp1float_decoder =
{
    "mp1float",
    AVMEDIA_TYPE_AUDIO,
    CODEC_ID_MP1,
    sizeof(MPADecodeContext),
    decode_init,
    NULL,
    decode_end,
    decode_frame,
    CODEC_CAP_PARSE_ONLY,
    .flush= flush,
    .long_name= NULL_IF_CONFIG_SMALL("MP1 (MPEG audio layer 1)"),
};
#endif
#if CONFIG_MP2FLOAT_DECODER
AVCodec mp2float_decoder =
{
    "mp2float",
    AVMEDIA_TYPE_AUDIO,
    CODEC_ID_MP2,
    sizeof(MPADecodeContext),
    decode_init,
    NULL,
    decode_end,
    decode_frame,
    CODEC_CAP_PARSE_ONLY,
    .flush= flush,
    .long_name= NULL_IF_CONFIG_SMALL("MP2 (MPEG audio layer 2)"),
};
#endif
#if CONFIG_MP3FLOAT_DECODER
AVCodec mp3float_decoder =
{
    "mp3float",
    AVMEDIA_TYPE_AUDIO,
    CODEC_ID_MP3,
    sizeof(MPADecodeContext),
    decode_init,
    NULL,
    decode_end,
    decode_frame,
    CODEC_CAP_PARSE_ONLY,
    .flush= flush,
    .long_name= NULL_IF_CONFIG_SMALL("MP3 (MPEG audio layer 3)"),
};
#endif
#if CONFIG_MP3ADUFLOAT_DECODER
AVCodec mp3adufloat_decoder =
{
    "mp3adufloat",
    AVMEDIA_TYPE_AUDIO,
    CODEC_ID_MP3ADU,
    sizeof(MPADecodeContext),
    decode_init,
    NULL,
    decode_end,
    decode_frame_adu,
    CODEC_CAP_PARSE_ONLY,
    .flush= flush,
    .long_name= NULL_IF_CONFIG_SMALL("ADU (Application Data Unit) MP3 (MPEG audio layer 3)"),
};
#endif
#if CONFIG_MP3ON4FLOAT_DECODER
AVCodec mp3on4float_decoder =
{
    "mp3on4float",
    AVMEDIA_TYPE_AUDIO,
    CODEC_ID_MP3ON4,
    sizeof(MP3On4DecodeContext),
    decode_init_mp3on4,
    NULL,
    decode_close_mp3on4,
    decode_frame_mp3on4,
    .flush= flush,
    .long_name= NULL_IF_CONFIG_SMALL("MP3onMP4"),
};
#endif
