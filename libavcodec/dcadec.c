/*
 * Copyright (C) 2016 foo86
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

#include "libavutil/opt.h"
#include "libavutil/channel_layout.h"
#include "libavutil/thread.h"

#include "codec_internal.h"
#include "dcadec.h"
#include "dcahuff.h"
#include "dca_syncwords.h"
#include "profiles.h"

#define MIN_PACKET_SIZE     16
#define MAX_PACKET_SIZE     0x104000

int ff_dca_set_channel_layout(AVCodecContext *avctx, int *ch_remap, int dca_mask)
{
    static const uint8_t dca2wav_norm[28] = {
         2,  0, 1, 9, 10,  3,  8,  4,  5,  9, 10, 6, 7, 12,
        13, 14, 3, 6,  7, 11, 12, 14, 16, 15, 17, 8, 4,  5,
    };

    static const uint8_t dca2wav_wide[28] = {
         2,  0, 1, 4,  5,  3,  8,  4,  5,  9, 10, 6, 7, 12,
        13, 14, 3, 9, 10, 11, 12, 14, 16, 15, 17, 8, 4,  5,
    };

    DCAContext *s = avctx->priv_data;

    int dca_ch, wav_ch, nchannels = 0;

    av_channel_layout_uninit(&avctx->ch_layout);
    if (s->output_channel_order == CHANNEL_ORDER_CODED) {
        for (dca_ch = 0; dca_ch < DCA_SPEAKER_COUNT; dca_ch++)
            if (dca_mask & (1U << dca_ch))
                ch_remap[nchannels++] = dca_ch;
        avctx->ch_layout.order       = AV_CHANNEL_ORDER_UNSPEC;
        avctx->ch_layout.nb_channels = nchannels;
    } else {
        int wav_mask = 0;
        int wav_map[18];
        const uint8_t *dca2wav;
        if (dca_mask == DCA_SPEAKER_LAYOUT_7POINT0_WIDE ||
            dca_mask == DCA_SPEAKER_LAYOUT_7POINT1_WIDE)
            dca2wav = dca2wav_wide;
        else
            dca2wav = dca2wav_norm;
        for (dca_ch = 0; dca_ch < 28; dca_ch++) {
            if (dca_mask & (1 << dca_ch)) {
                wav_ch = dca2wav[dca_ch];
                if (!(wav_mask & (1 << wav_ch))) {
                    wav_map[wav_ch] = dca_ch;
                    wav_mask |= 1 << wav_ch;
                }
            }
        }
        for (wav_ch = 0; wav_ch < 18; wav_ch++)
            if (wav_mask & (1 << wav_ch))
                ch_remap[nchannels++] = wav_map[wav_ch];

        av_channel_layout_from_mask(&avctx->ch_layout, wav_mask);
    }

    return nchannels;
}

void ff_dca_downmix_to_stereo_fixed(DCADSPContext *dcadsp, int32_t **samples,
                                    int *coeff_l, int nsamples, int ch_mask)
{
    int pos, spkr, max_spkr = av_log2(ch_mask);
    int *coeff_r = coeff_l + av_popcount(ch_mask);

    av_assert0(DCA_HAS_STEREO(ch_mask));

    // Scale left and right channels
    pos = (ch_mask & DCA_SPEAKER_MASK_C);
    dcadsp->dmix_scale(samples[DCA_SPEAKER_L], coeff_l[pos    ], nsamples);
    dcadsp->dmix_scale(samples[DCA_SPEAKER_R], coeff_r[pos + 1], nsamples);

    // Downmix remaining channels
    for (spkr = 0; spkr <= max_spkr; spkr++) {
        if (!(ch_mask & (1U << spkr)))
            continue;

        if (*coeff_l && spkr != DCA_SPEAKER_L)
            dcadsp->dmix_add(samples[DCA_SPEAKER_L], samples[spkr],
                             *coeff_l, nsamples);

        if (*coeff_r && spkr != DCA_SPEAKER_R)
            dcadsp->dmix_add(samples[DCA_SPEAKER_R], samples[spkr],
                             *coeff_r, nsamples);

        coeff_l++;
        coeff_r++;
    }
}

void ff_dca_downmix_to_stereo_float(AVFloatDSPContext *fdsp, float **samples,
                                    int *coeff_l, int nsamples, int ch_mask)
{
    int pos, spkr, max_spkr = av_log2(ch_mask);
    int *coeff_r = coeff_l + av_popcount(ch_mask);
    const float scale = 1.0f / (1 << 15);

    av_assert0(DCA_HAS_STEREO(ch_mask));

    // Scale left and right channels
    pos = (ch_mask & DCA_SPEAKER_MASK_C);
    fdsp->vector_fmul_scalar(samples[DCA_SPEAKER_L], samples[DCA_SPEAKER_L],
                             coeff_l[pos    ] * scale, nsamples);
    fdsp->vector_fmul_scalar(samples[DCA_SPEAKER_R], samples[DCA_SPEAKER_R],
                             coeff_r[pos + 1] * scale, nsamples);

    // Downmix remaining channels
    for (spkr = 0; spkr <= max_spkr; spkr++) {
        if (!(ch_mask & (1U << spkr)))
            continue;

        if (*coeff_l && spkr != DCA_SPEAKER_L)
            fdsp->vector_fmac_scalar(samples[DCA_SPEAKER_L], samples[spkr],
                                     *coeff_l * scale, nsamples);

        if (*coeff_r && spkr != DCA_SPEAKER_R)
            fdsp->vector_fmac_scalar(samples[DCA_SPEAKER_R], samples[spkr],
                                     *coeff_r * scale, nsamples);

        coeff_l++;
        coeff_r++;
    }
}

static int dcadec_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                               int *got_frame_ptr, AVPacket *avpkt)
{
    DCAContext *s = avctx->priv_data;
    uint8_t *input = avpkt->data;
    int input_size = avpkt->size;
    int i, ret, prev_packet = s->packet;
    uint32_t mrk;

    if (input_size < MIN_PACKET_SIZE || input_size > MAX_PACKET_SIZE) {
        av_log(avctx, AV_LOG_ERROR, "Invalid packet size\n");
        return AVERROR_INVALIDDATA;
    }

    // Convert input to BE format
    mrk = AV_RB32(input);
    if (mrk != DCA_SYNCWORD_CORE_BE && mrk != DCA_SYNCWORD_SUBSTREAM) {
        av_fast_padded_malloc(&s->buffer, &s->buffer_size, input_size);
        if (!s->buffer)
            return AVERROR(ENOMEM);

        for (i = 0, ret = AVERROR_INVALIDDATA; i < input_size - MIN_PACKET_SIZE + 1 && ret < 0; i++)
            ret = avpriv_dca_convert_bitstream(input + i, input_size - i, s->buffer, s->buffer_size);

        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "Not a valid DCA frame\n");
            return ret;
        }

        input      = s->buffer;
        input_size = ret;
    }

    s->packet = 0;

    // Parse backward compatible core sub-stream
    if (AV_RB32(input) == DCA_SYNCWORD_CORE_BE) {
        int frame_size;

        if ((ret = ff_dca_core_parse(&s->core, input, input_size)) < 0)
            return ret;

        s->packet |= DCA_PACKET_CORE;

        // EXXS data must be aligned on 4-byte boundary
        frame_size = FFALIGN(s->core.frame_size, 4);
        if (input_size - 4 > frame_size) {
            input      += frame_size;
            input_size -= frame_size;
        }
    }

    if (!s->core_only) {
        DCAExssAsset *asset = NULL;

        // Parse extension sub-stream (EXSS)
        if (AV_RB32(input) == DCA_SYNCWORD_SUBSTREAM) {
            if ((ret = ff_dca_exss_parse(&s->exss, input, input_size)) < 0) {
                if (avctx->err_recognition & AV_EF_EXPLODE)
                    return ret;
            } else {
                s->packet |= DCA_PACKET_EXSS;
                asset = &s->exss.assets[0];
            }
        }

        // Parse XLL component in EXSS
        if (asset && (asset->extension_mask & DCA_EXSS_XLL)) {
            if ((ret = ff_dca_xll_parse(&s->xll, input, asset)) < 0) {
                // Conceal XLL synchronization error
                if (ret == AVERROR(EAGAIN)
                    && (prev_packet & DCA_PACKET_XLL)
                    && (s->packet & DCA_PACKET_CORE))
                    s->packet |= DCA_PACKET_XLL | DCA_PACKET_RECOVERY;
                else if (ret == AVERROR(ENOMEM) || (avctx->err_recognition & AV_EF_EXPLODE))
                    return ret;
            } else {
                s->packet |= DCA_PACKET_XLL;
            }
        }

        // Parse LBR component in EXSS
        if (asset && (asset->extension_mask & DCA_EXSS_LBR)) {
            if ((ret = ff_dca_lbr_parse(&s->lbr, input, asset)) < 0) {
                if (ret == AVERROR(ENOMEM) || (avctx->err_recognition & AV_EF_EXPLODE))
                    return ret;
            } else {
                s->packet |= DCA_PACKET_LBR;
            }
        }

        // Parse core extensions in EXSS or backward compatible core sub-stream
        if ((s->packet & DCA_PACKET_CORE)
            && (ret = ff_dca_core_parse_exss(&s->core, input, asset)) < 0)
            return ret;
    }

    // Filter the frame
    if (s->packet & DCA_PACKET_LBR) {
        if ((ret = ff_dca_lbr_filter_frame(&s->lbr, frame)) < 0)
            return ret;
    } else if (s->packet & DCA_PACKET_XLL) {
        if (s->packet & DCA_PACKET_CORE) {
            int x96_synth = -1;

            // Enable X96 synthesis if needed
            if (s->xll.chset[0].freq == 96000 && s->core.sample_rate == 48000)
                x96_synth = 1;

            if ((ret = ff_dca_core_filter_fixed(&s->core, x96_synth)) < 0)
                return ret;

            // Force lossy downmixed output on the first core frame filtered.
            // This prevents audible clicks when seeking and is consistent with
            // what reference decoder does when there are multiple channel sets.
            if (!(prev_packet & DCA_PACKET_RESIDUAL) && s->xll.nreschsets > 0
                && s->xll.nchsets > 1) {
                av_log(avctx, AV_LOG_VERBOSE, "Forcing XLL recovery mode\n");
                s->packet |= DCA_PACKET_RECOVERY;
            }

            // Set 'residual ok' flag for the next frame
            s->packet |= DCA_PACKET_RESIDUAL;
        }

        if ((ret = ff_dca_xll_filter_frame(&s->xll, frame)) < 0) {
            // Fall back to core unless hard error
            if (!(s->packet & DCA_PACKET_CORE))
                return ret;
            if (ret != AVERROR_INVALIDDATA || (avctx->err_recognition & AV_EF_EXPLODE))
                return ret;
            if ((ret = ff_dca_core_filter_frame(&s->core, frame)) < 0)
                return ret;
        }
    } else if (s->packet & DCA_PACKET_CORE) {
        if ((ret = ff_dca_core_filter_frame(&s->core, frame)) < 0)
            return ret;
        if (s->core.filter_mode & DCA_FILTER_MODE_FIXED)
            s->packet |= DCA_PACKET_RESIDUAL;
    } else {
        av_log(avctx, AV_LOG_ERROR, "No valid DCA sub-stream found\n");
        if (s->core_only)
            av_log(avctx, AV_LOG_WARNING, "Consider disabling 'core_only' option\n");
        return AVERROR_INVALIDDATA;
    }

    *got_frame_ptr = 1;

    return avpkt->size;
}

static av_cold void dcadec_flush(AVCodecContext *avctx)
{
    DCAContext *s = avctx->priv_data;

    ff_dca_core_flush(&s->core);
    ff_dca_xll_flush(&s->xll);
    ff_dca_lbr_flush(&s->lbr);

    s->packet &= DCA_PACKET_MASK;
}

static av_cold int dcadec_close(AVCodecContext *avctx)
{
    DCAContext *s = avctx->priv_data;

    ff_dca_core_close(&s->core);
    ff_dca_xll_close(&s->xll);
    ff_dca_lbr_close(&s->lbr);

    av_freep(&s->buffer);
    s->buffer_size = 0;

    return 0;
}

static av_cold void dcadec_init_static(void)
{
    ff_dca_lbr_init_tables();
    ff_dca_init_vlcs();
}

static av_cold int dcadec_init(AVCodecContext *avctx)
{
    static AVOnce init_static_once = AV_ONCE_INIT;
    DCAContext *s = avctx->priv_data;

    s->avctx = avctx;
    s->core.avctx = avctx;
    s->exss.avctx = avctx;
    s->xll.avctx = avctx;
    s->lbr.avctx = avctx;

    if (ff_dca_core_init(&s->core) < 0)
        return AVERROR(ENOMEM);

    if (ff_dca_lbr_init(&s->lbr) < 0)
        return AVERROR(ENOMEM);

    ff_dcadsp_init(&s->dcadsp);
    s->core.dcadsp = &s->dcadsp;
    s->xll.dcadsp = &s->dcadsp;
    s->lbr.dcadsp = &s->dcadsp;

    s->crctab = av_crc_get_table(AV_CRC_16_CCITT);

#if FF_API_OLD_CHANNEL_LAYOUT
FF_DISABLE_DEPRECATION_WARNINGS
    if (avctx->request_channel_layout & AV_CH_LAYOUT_NATIVE)
        s->output_channel_order = CHANNEL_ORDER_CODED;

    if (avctx->request_channel_layout & ~AV_CH_LAYOUT_NATIVE) {
        av_channel_layout_uninit(&s->downmix_layout);
        av_channel_layout_from_mask(&s->downmix_layout, avctx->request_channel_layout & ~AV_CH_LAYOUT_NATIVE);
    }
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    if (s->downmix_layout.nb_channels) {
        if (!av_channel_layout_compare(&s->downmix_layout, &(AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO) ||
            !av_channel_layout_compare(&s->downmix_layout, &(AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO_DOWNMIX))
            s->request_channel_layout = DCA_SPEAKER_LAYOUT_STEREO;
        else if (!av_channel_layout_compare(&s->downmix_layout, &(AVChannelLayout)AV_CHANNEL_LAYOUT_5POINT0))
            s->request_channel_layout = DCA_SPEAKER_LAYOUT_5POINT0;
        else if (!av_channel_layout_compare(&s->downmix_layout, &(AVChannelLayout)AV_CHANNEL_LAYOUT_5POINT1))
            s->request_channel_layout = DCA_SPEAKER_LAYOUT_5POINT1;
        else
            av_log(avctx, AV_LOG_WARNING, "Invalid downmix layout\n");
    }

    ff_thread_once(&init_static_once, dcadec_init_static);

    return 0;
}

#define OFFSET(x) offsetof(DCAContext, x)
#define PARAM AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_DECODING_PARAM

static const AVOption dcadec_options[] = {
    { "core_only", "Decode core only without extensions", OFFSET(core_only), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, PARAM },

    { "channel_order", "Order in which the channels are to be exported",
        OFFSET(output_channel_order), AV_OPT_TYPE_INT,
        { .i64 = CHANNEL_ORDER_DEFAULT }, 0, 1, PARAM, "channel_order" },
      { "default", "normal libavcodec channel order", 0, AV_OPT_TYPE_CONST,
        { .i64 = CHANNEL_ORDER_DEFAULT }, .flags = PARAM, "channel_order" },
      { "coded",    "order in which the channels are coded in the bitstream",
        0, AV_OPT_TYPE_CONST, { .i64 = CHANNEL_ORDER_CODED }, .flags = PARAM, "channel_order" },

    { "downmix", "Request a specific channel layout from the decoder", OFFSET(downmix_layout),
        AV_OPT_TYPE_CHLAYOUT, {.str = NULL}, .flags = PARAM },

    { NULL }
};

static const AVClass dcadec_class = {
    .class_name = "DCA decoder",
    .item_name  = av_default_item_name,
    .option     = dcadec_options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_DECODER,
};

const FFCodec ff_dca_decoder = {
    .p.name         = "dca",
    .p.long_name    = NULL_IF_CONFIG_SMALL("DCA (DTS Coherent Acoustics)"),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_DTS,
    .priv_data_size = sizeof(DCAContext),
    .init           = dcadec_init,
    FF_CODEC_DECODE_CB(dcadec_decode_frame),
    .close          = dcadec_close,
    .flush          = dcadec_flush,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_CHANNEL_CONF,
    .p.sample_fmts  = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_S16P, AV_SAMPLE_FMT_S32P,
                                                      AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_NONE },
    .p.priv_class   = &dcadec_class,
    .p.profiles     = NULL_IF_CONFIG_SMALL(ff_dca_profiles),
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP,
};
