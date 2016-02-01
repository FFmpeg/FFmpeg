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

#include "dcadec.h"
#include "dcamath.h"
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

    int dca_ch, wav_ch, nchannels = 0;

    if (avctx->request_channel_layout & AV_CH_LAYOUT_NATIVE) {
        for (dca_ch = 0; dca_ch < DCA_SPEAKER_COUNT; dca_ch++)
            if (dca_mask & (1U << dca_ch))
                ch_remap[nchannels++] = dca_ch;
        avctx->channel_layout = dca_mask;
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
        avctx->channel_layout = wav_mask;
    }

    avctx->channels = nchannels;
    return nchannels;
}

static uint16_t crc16(const uint8_t *data, int size)
{
    static const uint16_t crctab[16] = {
        0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
        0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef,
    };

    uint16_t res = 0xffff;
    int i;

    for (i = 0; i < size; i++) {
        res = (res << 4) ^ crctab[(data[i] >> 4) ^ (res >> 12)];
        res = (res << 4) ^ crctab[(data[i] & 15) ^ (res >> 12)];
    }

    return res;
}

int ff_dca_check_crc(GetBitContext *s, int p1, int p2)
{
    if (((p1 | p2) & 7) || p1 < 0 || p2 > s->size_in_bits || p2 - p1 < 16)
        return -1;
    if (crc16(s->buffer + p1 / 8, (p2 - p1) / 8))
        return -1;
    return 0;
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

static int convert_bitstream(const uint8_t *src, int src_size, uint8_t *dst, int max_size)
{
    switch (AV_RB32(src)) {
    case DCA_SYNCWORD_CORE_BE:
    case DCA_SYNCWORD_SUBSTREAM:
        memcpy(dst, src, src_size);
        return src_size;
    case DCA_SYNCWORD_CORE_LE:
    case DCA_SYNCWORD_CORE_14B_BE:
    case DCA_SYNCWORD_CORE_14B_LE:
        return avpriv_dca_convert_bitstream(src, src_size, dst, max_size);
    default:
        return AVERROR_INVALIDDATA;
    }
}

static int dcadec_decode_frame(AVCodecContext *avctx, void *data,
                               int *got_frame_ptr, AVPacket *avpkt)
{
    DCAContext *s = avctx->priv_data;
    AVFrame *frame = data;
    uint8_t *input = avpkt->data;
    int input_size = avpkt->size;
    int i, ret, prev_packet = s->packet;

    if (input_size < MIN_PACKET_SIZE || input_size > MAX_PACKET_SIZE) {
        av_log(avctx, AV_LOG_ERROR, "Invalid packet size\n");
        return AVERROR_INVALIDDATA;
    }

    av_fast_malloc(&s->buffer, &s->buffer_size,
                   FFALIGN(input_size, 4096) + DCA_BUFFER_PADDING_SIZE);
    if (!s->buffer)
        return AVERROR(ENOMEM);

    for (i = 0, ret = AVERROR_INVALIDDATA; i < input_size - MIN_PACKET_SIZE + 1 && ret < 0; i++)
        ret = convert_bitstream(input + i, input_size - i, s->buffer, s->buffer_size);

    if (ret < 0)
        return ret;

    input      = s->buffer;
    input_size = ret;

    s->packet = 0;

    // Parse backward compatible core sub-stream
    if (AV_RB32(input) == DCA_SYNCWORD_CORE_BE) {
        int frame_size;

        if ((ret = ff_dca_core_parse(&s->core, input, input_size)) < 0) {
            s->core_residual_valid = 0;
            return ret;
        }

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

        // Parse core extensions in EXSS or backward compatible core sub-stream
        if ((s->packet & DCA_PACKET_CORE)
            && (ret = ff_dca_core_parse_exss(&s->core, input, asset)) < 0)
            return ret;
    }

    // Filter the frame
    if (s->packet & DCA_PACKET_XLL) {
        if (s->packet & DCA_PACKET_CORE) {
            int x96_synth = -1;

            // Enable X96 synthesis if needed
            if (s->xll.chset[0].freq == 96000 && s->core.sample_rate == 48000)
                x96_synth = 1;

            if ((ret = ff_dca_core_filter_fixed(&s->core, x96_synth)) < 0) {
                s->core_residual_valid = 0;
                return ret;
            }

            // Force lossy downmixed output on the first core frame filtered.
            // This prevents audible clicks when seeking and is consistent with
            // what reference decoder does when there are multiple channel sets.
            if (!s->core_residual_valid) {
                if (s->xll.nreschsets > 0 && s->xll.nchsets > 1)
                    s->packet |= DCA_PACKET_RECOVERY;
                s->core_residual_valid = 1;
            }
        }

        if ((ret = ff_dca_xll_filter_frame(&s->xll, frame)) < 0) {
            // Fall back to core unless hard error
            if (!(s->packet & DCA_PACKET_CORE))
                return ret;
            if (ret != AVERROR_INVALIDDATA || (avctx->err_recognition & AV_EF_EXPLODE))
                return ret;
            if ((ret = ff_dca_core_filter_frame(&s->core, frame)) < 0) {
                s->core_residual_valid = 0;
                return ret;
            }
        }
    } else if (s->packet & DCA_PACKET_CORE) {
        if ((ret = ff_dca_core_filter_frame(&s->core, frame)) < 0) {
            s->core_residual_valid = 0;
            return ret;
        }
        s->core_residual_valid = !!(s->core.filter_mode & DCA_FILTER_MODE_FIXED);
    } else {
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

    s->core_residual_valid = 0;
}

static av_cold int dcadec_close(AVCodecContext *avctx)
{
    DCAContext *s = avctx->priv_data;

    ff_dca_core_close(&s->core);
    ff_dca_xll_close(&s->xll);

    av_freep(&s->buffer);
    s->buffer_size = 0;

    return 0;
}

static av_cold int dcadec_init(AVCodecContext *avctx)
{
    DCAContext *s = avctx->priv_data;

    s->avctx = avctx;
    s->core.avctx = avctx;
    s->exss.avctx = avctx;
    s->xll.avctx = avctx;

    if (ff_dca_core_init(&s->core) < 0)
        return AVERROR(ENOMEM);

    ff_dcadsp_init(&s->dcadsp);
    s->core.dcadsp = &s->dcadsp;
    s->xll.dcadsp = &s->dcadsp;

    switch (avctx->request_channel_layout & ~AV_CH_LAYOUT_NATIVE) {
    case 0:
        s->request_channel_layout = 0;
        break;
    case AV_CH_LAYOUT_STEREO:
    case AV_CH_LAYOUT_STEREO_DOWNMIX:
        s->request_channel_layout = DCA_SPEAKER_LAYOUT_STEREO;
        break;
    case AV_CH_LAYOUT_5POINT0:
        s->request_channel_layout = DCA_SPEAKER_LAYOUT_5POINT0;
        break;
    case AV_CH_LAYOUT_5POINT1:
        s->request_channel_layout = DCA_SPEAKER_LAYOUT_5POINT1;
        break;
    default:
        av_log(avctx, AV_LOG_WARNING, "Invalid request_channel_layout\n");
        break;
    }

    avctx->sample_fmt = AV_SAMPLE_FMT_S32P;
    avctx->bits_per_raw_sample = 24;

    return 0;
}

#define OFFSET(x) offsetof(DCAContext, x)
#define PARAM AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_DECODING_PARAM

static const AVOption dcadec_options[] = {
    { "core_only", "Decode core only without extensions", OFFSET(core_only), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, PARAM },
    { NULL }
};

static const AVClass dcadec_class = {
    .class_name = "DCA decoder",
    .item_name  = av_default_item_name,
    .option     = dcadec_options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_DECODER,
};

AVCodec ff_dca_decoder = {
    .name           = "dca",
    .long_name      = NULL_IF_CONFIG_SMALL("DCA (DTS Coherent Acoustics)"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_DTS,
    .priv_data_size = sizeof(DCAContext),
    .init           = dcadec_init,
    .decode         = dcadec_decode_frame,
    .close          = dcadec_close,
    .flush          = dcadec_flush,
    .capabilities   = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_CHANNEL_CONF,
    .sample_fmts    = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_S16P, AV_SAMPLE_FMT_S32P,
                                                      AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_NONE },
    .priv_class     = &dcadec_class,
    .profiles       = NULL_IF_CONFIG_SMALL(ff_dca_profiles),
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
};
