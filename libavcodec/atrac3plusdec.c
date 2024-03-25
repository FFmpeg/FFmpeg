/*
 * ATRAC3+ compatible decoder
 *
 * Copyright (c) 2010-2013 Maxim Poliakovski
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
 * Sony ATRAC3+ compatible decoder.
 *
 * Container formats used to store its data:
 * RIFF WAV (.at3) and Sony OpenMG (.oma, .aa3).
 *
 * Technical description of this codec can be found here:
 * http://wiki.multimedia.cx/index.php?title=ATRAC3plus
 *
 * Kudos to Benjamin Larsson and Michael Karcher
 * for their precious technical help!
 */

#include <stdint.h>
#include <string.h>

#include "libavutil/channel_layout.h"
#include "libavutil/float_dsp.h"
#include "libavutil/mem.h"
#include "libavutil/mem_internal.h"
#include "libavutil/thread.h"
#include "avcodec.h"
#include "codec_internal.h"
#include "decode.h"
#include "get_bits.h"
#include "atrac.h"
#include "atrac3plus.h"

static const uint8_t channel_map[8][8] = {
    { 0, },
    { 0, 1, },
    { 0, 1, 2, },
    { 0, 1, 2, 3, },
    { 0, },
    { 0, 1, 2, 4, 5, 3, },
    { 0, 1, 2, 4, 5, 6, 3, },
    { 0, 1, 2, 4, 5, 6, 7, 3, },
};

typedef struct ATRAC3PContext {
    GetBitContext gb;
    AVFloatDSPContext *fdsp;

    DECLARE_ALIGNED(32, float, samples)[2][ATRAC3P_FRAME_SAMPLES];  ///< quantized MDCT spectrum
    DECLARE_ALIGNED(32, float, mdct_buf)[2][ATRAC3P_FRAME_SAMPLES]; ///< output of the IMDCT
    DECLARE_ALIGNED(32, float, time_buf)[2][ATRAC3P_FRAME_SAMPLES]; ///< output of the gain compensation
    DECLARE_ALIGNED(32, float, outp_buf)[2][ATRAC3P_FRAME_SAMPLES];

    AtracGCContext gainc_ctx;   ///< gain compensation context
    AVTXContext *mdct_ctx;
    av_tx_fn mdct_fn;
    AVTXContext *ipqf_dct_ctx;  ///< IDCT context used by IPQF
    av_tx_fn ipqf_dct_fn;

    Atrac3pChanUnitCtx *ch_units;   ///< global channel units

    int num_channel_blocks;     ///< number of channel blocks
    uint8_t channel_blocks[5];  ///< channel configuration descriptor
    const uint8_t *channel_map; ///< channel layout map
} ATRAC3PContext;

static av_cold int atrac3p_decode_close(AVCodecContext *avctx)
{
    ATRAC3PContext *ctx = avctx->priv_data;

    av_freep(&ctx->ch_units);
    av_freep(&ctx->fdsp);

    av_tx_uninit(&ctx->mdct_ctx);
    av_tx_uninit(&ctx->ipqf_dct_ctx);

    return 0;
}

static av_cold int set_channel_params(ATRAC3PContext *ctx,
                                      AVCodecContext *avctx)
{
    int channels = avctx->ch_layout.nb_channels;
    memset(ctx->channel_blocks, 0, sizeof(ctx->channel_blocks));

    av_channel_layout_uninit(&avctx->ch_layout);
    switch (channels) {
    case 1:
        avctx->ch_layout        = (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;
        ctx->num_channel_blocks = 1;
        ctx->channel_blocks[0]  = CH_UNIT_MONO;
        break;
    case 2:
        avctx->ch_layout        = (AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO;
        ctx->num_channel_blocks = 1;
        ctx->channel_blocks[0]  = CH_UNIT_STEREO;
        break;
    case 3:
        avctx->ch_layout        = (AVChannelLayout)AV_CHANNEL_LAYOUT_SURROUND;
        ctx->num_channel_blocks = 2;
        ctx->channel_blocks[0]  = CH_UNIT_STEREO;
        ctx->channel_blocks[1]  = CH_UNIT_MONO;
        break;
    case 4:
        avctx->ch_layout        = (AVChannelLayout)AV_CHANNEL_LAYOUT_4POINT0;
        ctx->num_channel_blocks = 3;
        ctx->channel_blocks[0]  = CH_UNIT_STEREO;
        ctx->channel_blocks[1]  = CH_UNIT_MONO;
        ctx->channel_blocks[2]  = CH_UNIT_MONO;
        break;
    case 6:
        avctx->ch_layout        = (AVChannelLayout)AV_CHANNEL_LAYOUT_5POINT1_BACK;
        ctx->num_channel_blocks = 4;
        ctx->channel_blocks[0]  = CH_UNIT_STEREO;
        ctx->channel_blocks[1]  = CH_UNIT_MONO;
        ctx->channel_blocks[2]  = CH_UNIT_STEREO;
        ctx->channel_blocks[3]  = CH_UNIT_MONO;
        break;
    case 7:
        avctx->ch_layout        = (AVChannelLayout)AV_CHANNEL_LAYOUT_6POINT1_BACK;
        ctx->num_channel_blocks = 5;
        ctx->channel_blocks[0]  = CH_UNIT_STEREO;
        ctx->channel_blocks[1]  = CH_UNIT_MONO;
        ctx->channel_blocks[2]  = CH_UNIT_STEREO;
        ctx->channel_blocks[3]  = CH_UNIT_MONO;
        ctx->channel_blocks[4]  = CH_UNIT_MONO;
        break;
    case 8:
        avctx->ch_layout        = (AVChannelLayout)AV_CHANNEL_LAYOUT_7POINT1;
        ctx->num_channel_blocks = 5;
        ctx->channel_blocks[0]  = CH_UNIT_STEREO;
        ctx->channel_blocks[1]  = CH_UNIT_MONO;
        ctx->channel_blocks[2]  = CH_UNIT_STEREO;
        ctx->channel_blocks[3]  = CH_UNIT_STEREO;
        ctx->channel_blocks[4]  = CH_UNIT_MONO;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR,
               "Unsupported channel count: %d!\n", channels);
        return AVERROR_INVALIDDATA;
    }

    ctx->channel_map = channel_map[channels - 1];

    return 0;
}

static av_cold void atrac3p_init_static(void)
{
    ff_atrac3p_init_vlcs();
    ff_atrac3p_init_dsp_static();
}

static av_cold int atrac3p_decode_init(AVCodecContext *avctx)
{
    static AVOnce init_static_once = AV_ONCE_INIT;
    ATRAC3PContext *ctx = avctx->priv_data;
    float scale;
    int i, ch, ret;

    if (!avctx->block_align) {
        av_log(avctx, AV_LOG_ERROR, "block_align is not set\n");
        return AVERROR(EINVAL);
    }

    /* initialize IPQF */
    scale = 32.0 / 32768.0;
    ret = av_tx_init(&ctx->ipqf_dct_ctx, &ctx->ipqf_dct_fn, AV_TX_FLOAT_MDCT,
                     1, 16, &scale, 0);
    if (ret < 0)
        return ret;

    scale = -1.0f;
    ret = av_tx_init(&ctx->mdct_ctx, &ctx->mdct_fn, AV_TX_FLOAT_MDCT,
                     1, 128, &scale, AV_TX_FULL_IMDCT);
    if (ret < 0)
        return ret;

    ff_atrac_init_gain_compensation(&ctx->gainc_ctx, 6, 2);

    if ((ret = set_channel_params(ctx, avctx)) < 0)
        return ret;

    ctx->ch_units = av_calloc(ctx->num_channel_blocks, sizeof(*ctx->ch_units));
    ctx->fdsp = avpriv_float_dsp_alloc(avctx->flags & AV_CODEC_FLAG_BITEXACT);

    if (!ctx->ch_units || !ctx->fdsp) {
        return AVERROR(ENOMEM);
    }

    for (i = 0; i < ctx->num_channel_blocks; i++) {
        for (ch = 0; ch < 2; ch++) {
            ctx->ch_units[i].channels[ch].ch_num          = ch;
            ctx->ch_units[i].channels[ch].wnd_shape       = &ctx->ch_units[i].channels[ch].wnd_shape_hist[0][0];
            ctx->ch_units[i].channels[ch].wnd_shape_prev  = &ctx->ch_units[i].channels[ch].wnd_shape_hist[1][0];
            ctx->ch_units[i].channels[ch].gain_data       = &ctx->ch_units[i].channels[ch].gain_data_hist[0][0];
            ctx->ch_units[i].channels[ch].gain_data_prev  = &ctx->ch_units[i].channels[ch].gain_data_hist[1][0];
            ctx->ch_units[i].channels[ch].tones_info      = &ctx->ch_units[i].channels[ch].tones_info_hist[0][0];
            ctx->ch_units[i].channels[ch].tones_info_prev = &ctx->ch_units[i].channels[ch].tones_info_hist[1][0];
        }

        ctx->ch_units[i].waves_info      = &ctx->ch_units[i].wave_synth_hist[0];
        ctx->ch_units[i].waves_info_prev = &ctx->ch_units[i].wave_synth_hist[1];
    }

    avctx->sample_fmt = AV_SAMPLE_FMT_FLTP;

    ff_thread_once(&init_static_once, atrac3p_init_static);

    return 0;
}

static void decode_residual_spectrum(ATRAC3PContext *ctx, Atrac3pChanUnitCtx *ch_unit,
                                     float out[2][ATRAC3P_FRAME_SAMPLES],
                                     int num_channels,
                                     AVCodecContext *avctx)
{
    int i, sb, ch, qu, nspeclines, RNG_index;
    float *dst, q;
    int16_t *src;
    /* calculate RNG table index for each subband */
    int sb_RNG_index[ATRAC3P_SUBBANDS] = { 0 };

    if (ch_unit->mute_flag) {
        for (ch = 0; ch < num_channels; ch++)
            memset(out[ch], 0, ATRAC3P_FRAME_SAMPLES * sizeof(*out[ch]));
        return;
    }

    for (qu = 0, RNG_index = 0; qu < ch_unit->used_quant_units; qu++)
        RNG_index += ch_unit->channels[0].qu_sf_idx[qu] +
                     ch_unit->channels[1].qu_sf_idx[qu];

    for (sb = 0; sb < ch_unit->num_coded_subbands; sb++, RNG_index += 128)
        sb_RNG_index[sb] = RNG_index & 0x3FC;

    /* inverse quant and power compensation */
    for (ch = 0; ch < num_channels; ch++) {
        /* clear channel's residual spectrum */
        memset(out[ch], 0, ATRAC3P_FRAME_SAMPLES * sizeof(*out[ch]));

        for (qu = 0; qu < ch_unit->used_quant_units; qu++) {
            src        = &ch_unit->channels[ch].spectrum[ff_atrac3p_qu_to_spec_pos[qu]];
            dst        = &out[ch][ff_atrac3p_qu_to_spec_pos[qu]];
            nspeclines = ff_atrac3p_qu_to_spec_pos[qu + 1] -
                         ff_atrac3p_qu_to_spec_pos[qu];

            if (ch_unit->channels[ch].qu_wordlen[qu] > 0) {
                q = ff_atrac3p_sf_tab[ch_unit->channels[ch].qu_sf_idx[qu]] *
                    ff_atrac3p_mant_tab[ch_unit->channels[ch].qu_wordlen[qu]];
                for (i = 0; i < nspeclines; i++)
                    dst[i] = src[i] * q;
            }
        }

        for (sb = 0; sb < ch_unit->num_coded_subbands; sb++)
            ff_atrac3p_power_compensation(ch_unit, ctx->fdsp, ch, &out[ch][0],
                                          sb_RNG_index[sb], sb);
    }

    if (ch_unit->unit_type == CH_UNIT_STEREO) {
        for (sb = 0; sb < ch_unit->num_coded_subbands; sb++) {
            if (ch_unit->swap_channels[sb]) {
                for (i = 0; i < ATRAC3P_SUBBAND_SAMPLES; i++)
                    FFSWAP(float, out[0][sb * ATRAC3P_SUBBAND_SAMPLES + i],
                                  out[1][sb * ATRAC3P_SUBBAND_SAMPLES + i]);
            }

            /* flip coefficients' sign if requested */
            if (ch_unit->negate_coeffs[sb])
                for (i = 0; i < ATRAC3P_SUBBAND_SAMPLES; i++)
                    out[1][sb * ATRAC3P_SUBBAND_SAMPLES + i] = -(out[1][sb * ATRAC3P_SUBBAND_SAMPLES + i]);
        }
    }
}

static void reconstruct_frame(ATRAC3PContext *ctx, Atrac3pChanUnitCtx *ch_unit,
                              int num_channels, AVCodecContext *avctx)
{
    int ch, sb;

    for (ch = 0; ch < num_channels; ch++) {
        for (sb = 0; sb < ch_unit->num_subbands; sb++) {
            /* inverse transform and windowing */
            ff_atrac3p_imdct(ctx->fdsp, ctx->mdct_ctx, ctx->mdct_fn,
                             &ctx->samples[ch][sb * ATRAC3P_SUBBAND_SAMPLES],
                             &ctx->mdct_buf[ch][sb * ATRAC3P_SUBBAND_SAMPLES],
                             (ch_unit->channels[ch].wnd_shape_prev[sb] << 1) +
                             ch_unit->channels[ch].wnd_shape[sb], sb);

            /* gain compensation and overlapping */
            ff_atrac_gain_compensation(&ctx->gainc_ctx,
                                       &ctx->mdct_buf[ch][sb * ATRAC3P_SUBBAND_SAMPLES],
                                       &ch_unit->prev_buf[ch][sb * ATRAC3P_SUBBAND_SAMPLES],
                                       &ch_unit->channels[ch].gain_data_prev[sb],
                                       &ch_unit->channels[ch].gain_data[sb],
                                       ATRAC3P_SUBBAND_SAMPLES,
                                       &ctx->time_buf[ch][sb * ATRAC3P_SUBBAND_SAMPLES]);
        }

        /* zero unused subbands in both output and overlapping buffers */
        memset(&ch_unit->prev_buf[ch][ch_unit->num_subbands * ATRAC3P_SUBBAND_SAMPLES],
               0,
               (ATRAC3P_SUBBANDS - ch_unit->num_subbands) *
               ATRAC3P_SUBBAND_SAMPLES *
               sizeof(ch_unit->prev_buf[ch][ch_unit->num_subbands * ATRAC3P_SUBBAND_SAMPLES]));
        memset(&ctx->time_buf[ch][ch_unit->num_subbands * ATRAC3P_SUBBAND_SAMPLES],
               0,
               (ATRAC3P_SUBBANDS - ch_unit->num_subbands) *
               ATRAC3P_SUBBAND_SAMPLES *
               sizeof(ctx->time_buf[ch][ch_unit->num_subbands * ATRAC3P_SUBBAND_SAMPLES]));

        /* resynthesize and add tonal signal */
        if (ch_unit->waves_info->tones_present ||
            ch_unit->waves_info_prev->tones_present) {
            for (sb = 0; sb < ch_unit->num_subbands; sb++)
                if (ch_unit->channels[ch].tones_info[sb].num_wavs ||
                    ch_unit->channels[ch].tones_info_prev[sb].num_wavs) {
                    ff_atrac3p_generate_tones(ch_unit, ctx->fdsp, ch, sb,
                                              &ctx->time_buf[ch][sb * 128]);
                }
        }

        /* subband synthesis and acoustic signal output */
        ff_atrac3p_ipqf(ctx->ipqf_dct_ctx, ctx->ipqf_dct_fn,
                        &ch_unit->ipqf_ctx[ch], &ctx->time_buf[ch][0],
                        &ctx->outp_buf[ch][0]);
    }

    /* swap window shape and gain control buffers. */
    for (ch = 0; ch < num_channels; ch++) {
        FFSWAP(uint8_t *, ch_unit->channels[ch].wnd_shape,
               ch_unit->channels[ch].wnd_shape_prev);
        FFSWAP(AtracGainInfo *, ch_unit->channels[ch].gain_data,
               ch_unit->channels[ch].gain_data_prev);
        FFSWAP(Atrac3pWavesData *, ch_unit->channels[ch].tones_info,
               ch_unit->channels[ch].tones_info_prev);
    }

    FFSWAP(Atrac3pWaveSynthParams *, ch_unit->waves_info, ch_unit->waves_info_prev);
}

static int atrac3p_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                                int *got_frame_ptr, AVPacket *avpkt)
{
    ATRAC3PContext *ctx = avctx->priv_data;
    int i, ret, ch_unit_id, ch_block = 0, out_ch_index = 0, channels_to_process;
    float **samples_p = (float **)frame->extended_data;

    frame->nb_samples = ATRAC3P_FRAME_SAMPLES;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    if ((ret = init_get_bits8(&ctx->gb, avpkt->data, avpkt->size)) < 0)
        return ret;

    if (get_bits1(&ctx->gb)) {
        av_log(avctx, AV_LOG_ERROR, "Invalid start bit!\n");
        return AVERROR_INVALIDDATA;
    }

    while (get_bits_left(&ctx->gb) >= 2 &&
           (ch_unit_id = get_bits(&ctx->gb, 2)) != CH_UNIT_TERMINATOR) {
        if (ch_unit_id == CH_UNIT_EXTENSION) {
            avpriv_report_missing_feature(avctx, "Channel unit extension");
            return AVERROR_PATCHWELCOME;
        }
        if (ch_block >= ctx->num_channel_blocks ||
            ctx->channel_blocks[ch_block] != ch_unit_id) {
            av_log(avctx, AV_LOG_ERROR,
                   "Frame data doesn't match channel configuration!\n");
            return AVERROR_INVALIDDATA;
        }

        ctx->ch_units[ch_block].unit_type = ch_unit_id;
        channels_to_process               = ch_unit_id + 1;

        if ((ret = ff_atrac3p_decode_channel_unit(&ctx->gb,
                                                  &ctx->ch_units[ch_block],
                                                  channels_to_process,
                                                  avctx)) < 0)
            return ret;

        decode_residual_spectrum(ctx, &ctx->ch_units[ch_block], ctx->samples,
                                 channels_to_process, avctx);
        reconstruct_frame(ctx, &ctx->ch_units[ch_block],
                          channels_to_process, avctx);

        for (i = 0; i < channels_to_process; i++)
            memcpy(samples_p[ctx->channel_map[out_ch_index + i]], ctx->outp_buf[i],
                   ATRAC3P_FRAME_SAMPLES * sizeof(**samples_p));

        ch_block++;
        out_ch_index += channels_to_process;
    }

    *got_frame_ptr = 1;

    return avctx->codec_id == AV_CODEC_ID_ATRAC3P ? FFMIN(avctx->block_align, avpkt->size) : avpkt->size;
}

const FFCodec ff_atrac3p_decoder = {
    .p.name         = "atrac3plus",
    CODEC_LONG_NAME("ATRAC3+ (Adaptive TRansform Acoustic Coding 3+)"),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_ATRAC3P,
    .p.capabilities = AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
    .priv_data_size = sizeof(ATRAC3PContext),
    .init           = atrac3p_decode_init,
    .close          = atrac3p_decode_close,
    FF_CODEC_DECODE_CB(atrac3p_decode_frame),
};

const FFCodec ff_atrac3pal_decoder = {
    .p.name         = "atrac3plusal",
    CODEC_LONG_NAME("ATRAC3+ AL (Adaptive TRansform Acoustic Coding 3+ Advanced Lossless)"),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_ATRAC3PAL,
    .p.capabilities = AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
    .priv_data_size = sizeof(ATRAC3PContext),
    .init           = atrac3p_decode_init,
    .close          = atrac3p_decode_close,
    FF_CODEC_DECODE_CB(atrac3p_decode_frame),
};
