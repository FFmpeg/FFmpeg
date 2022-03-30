/*
 * Bluetooth low-complexity, subband codec (SBC)
 *
 * Copyright (C) 2017  Aurelien Jacobs <aurel@gnuage.org>
 * Copyright (C) 2012-2013  Intel Corporation
 * Copyright (C) 2008-2010  Nokia Corporation
 * Copyright (C) 2004-2010  Marcel Holtmann <marcel@holtmann.org>
 * Copyright (C) 2004-2005  Henryk Ploetz <henryk@ploetzli.ch>
 * Copyright (C) 2005-2008  Brad Midgley <bmidgley@xmission.com>
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
 * SBC encoder implementation
 */

#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"
#include "avcodec.h"
#include "codec_internal.h"
#include "encode.h"
#include "profiles.h"
#include "put_bits.h"
#include "sbc.h"
#include "sbcdsp.h"

typedef struct SBCEncContext {
    AVClass *class;
    int64_t max_delay;
    int msbc;
    DECLARE_ALIGNED(SBC_ALIGN, struct sbc_frame, frame);
    DECLARE_ALIGNED(SBC_ALIGN, SBCDSPContext, dsp);
} SBCEncContext;

static int sbc_analyze_audio(SBCDSPContext *s, struct sbc_frame *frame)
{
    int ch, blk;
    int16_t *x;

    switch (frame->subbands) {
    case 4:
        for (ch = 0; ch < frame->channels; ch++) {
            x = &s->X[ch][s->position - 4 *
                    s->increment + frame->blocks * 4];
            for (blk = 0; blk < frame->blocks;
                        blk += s->increment) {
                s->sbc_analyze_4s(
                    s, x,
                    frame->sb_sample_f[blk][ch],
                    frame->sb_sample_f[blk + 1][ch] -
                    frame->sb_sample_f[blk][ch]);
                x -= 4 * s->increment;
            }
        }
        return frame->blocks * 4;

    case 8:
        for (ch = 0; ch < frame->channels; ch++) {
            x = &s->X[ch][s->position - 8 *
                    s->increment + frame->blocks * 8];
            for (blk = 0; blk < frame->blocks;
                        blk += s->increment) {
                s->sbc_analyze_8s(
                    s, x,
                    frame->sb_sample_f[blk][ch],
                    frame->sb_sample_f[blk + 1][ch] -
                    frame->sb_sample_f[blk][ch]);
                x -= 8 * s->increment;
            }
        }
        return frame->blocks * 8;

    default:
        return AVERROR(EIO);
    }
}

/*
 * Packs the SBC frame from frame into the memory in avpkt.
 * Returns the length of the packed frame.
 */
static size_t sbc_pack_frame(AVPacket *avpkt, struct sbc_frame *frame,
                             int joint, int msbc)
{
    PutBitContext pb;

    /* Will copy the header parts for CRC-8 calculation here */
    uint8_t crc_header[11] = { 0 };
    int crc_pos;

    uint32_t audio_sample;

    int ch, sb, blk;        /* channel, subband, block and bit counters */
    int bits[2][8];         /* bits distribution */
    uint32_t levels[2][8];  /* levels are derived from that */
    uint32_t sb_sample_delta[2][8];

    if (msbc) {
        avpkt->data[0] = MSBC_SYNCWORD;
        avpkt->data[1] = 0;
        avpkt->data[2] = 0;
    } else {
        avpkt->data[0] = SBC_SYNCWORD;

        avpkt->data[1]  = (frame->frequency           & 0x03) << 6;
        avpkt->data[1] |= (((frame->blocks >> 2) - 1) & 0x03) << 4;
        avpkt->data[1] |= (frame->mode                & 0x03) << 2;
        avpkt->data[1] |= (frame->allocation          & 0x01) << 1;
        avpkt->data[1] |= ((frame->subbands == 8)     & 0x01) << 0;

        avpkt->data[2] = frame->bitpool;

        if (frame->bitpool > frame->subbands << (4 + (frame->mode == STEREO
                                                   || frame->mode == JOINT_STEREO)))
            return -5;
    }

    /* Can't fill in crc yet */
    crc_header[0] = avpkt->data[1];
    crc_header[1] = avpkt->data[2];
    crc_pos = 16;

    init_put_bits(&pb, avpkt->data + 4, avpkt->size);

    if (frame->mode == JOINT_STEREO) {
        put_bits(&pb, frame->subbands, joint);
        crc_header[crc_pos >> 3] = joint;
        crc_pos += frame->subbands;
    }

    for (ch = 0; ch < frame->channels; ch++) {
        for (sb = 0; sb < frame->subbands; sb++) {
            put_bits(&pb, 4, frame->scale_factor[ch][sb] & 0x0F);
            crc_header[crc_pos >> 3] <<= 4;
            crc_header[crc_pos >> 3] |= frame->scale_factor[ch][sb] & 0x0F;
            crc_pos += 4;
        }
    }

    /* align the last crc byte */
    if (crc_pos % 8)
        crc_header[crc_pos >> 3] <<= 8 - (crc_pos % 8);

    avpkt->data[3] = ff_sbc_crc8(frame->crc_ctx, crc_header, crc_pos);

    ff_sbc_calculate_bits(frame, bits);

    for (ch = 0; ch < frame->channels; ch++) {
        for (sb = 0; sb < frame->subbands; sb++) {
            levels[ch][sb] = ((1 << bits[ch][sb]) - 1) <<
                (32 - (frame->scale_factor[ch][sb] +
                    SCALE_OUT_BITS + 2));
            sb_sample_delta[ch][sb] = (uint32_t) 1 <<
                (frame->scale_factor[ch][sb] +
                    SCALE_OUT_BITS + 1);
        }
    }

    for (blk = 0; blk < frame->blocks; blk++) {
        for (ch = 0; ch < frame->channels; ch++) {
            for (sb = 0; sb < frame->subbands; sb++) {

                if (bits[ch][sb] == 0)
                    continue;

                audio_sample = ((uint64_t) levels[ch][sb] *
                    (sb_sample_delta[ch][sb] +
                    frame->sb_sample_f[blk][ch][sb])) >> 32;

                put_bits(&pb, bits[ch][sb], audio_sample);
            }
        }
    }

    flush_put_bits(&pb);

    return put_bytes_output(&pb);
}

static int sbc_encode_init(AVCodecContext *avctx)
{
    SBCEncContext *sbc = avctx->priv_data;
    struct sbc_frame *frame = &sbc->frame;

    if (avctx->profile == FF_PROFILE_SBC_MSBC)
        sbc->msbc = 1;

    if (sbc->msbc) {
        if (avctx->ch_layout.nb_channels != 1) {
            av_log(avctx, AV_LOG_ERROR, "mSBC require mono channel.\n");
            return AVERROR(EINVAL);
        }

        if (avctx->sample_rate != 16000) {
            av_log(avctx, AV_LOG_ERROR, "mSBC require 16 kHz samplerate.\n");
            return AVERROR(EINVAL);
        }

        frame->mode = SBC_MODE_MONO;
        frame->subbands = 8;
        frame->blocks = MSBC_BLOCKS;
        frame->allocation = SBC_AM_LOUDNESS;
        frame->bitpool = 26;

        avctx->frame_size = 8 * MSBC_BLOCKS;
    } else {
        int d;

        if (avctx->global_quality > 255*FF_QP2LAMBDA) {
            av_log(avctx, AV_LOG_ERROR, "bitpool > 255 is not allowed.\n");
            return AVERROR(EINVAL);
        }

        if (avctx->ch_layout.nb_channels == 1) {
            frame->mode = SBC_MODE_MONO;
            if (sbc->max_delay <= 3000 || avctx->bit_rate > 270000)
                frame->subbands = 4;
            else
                frame->subbands = 8;
        } else {
            if (avctx->bit_rate < 180000 || avctx->bit_rate > 420000)
                frame->mode = SBC_MODE_JOINT_STEREO;
            else
                frame->mode = SBC_MODE_STEREO;
            if (sbc->max_delay <= 4000 || avctx->bit_rate > 420000)
                frame->subbands = 4;
            else
                frame->subbands = 8;
        }
        /* sbc algorithmic delay is ((blocks + 10) * subbands - 2) / sample_rate */
        frame->blocks = av_clip(((sbc->max_delay * avctx->sample_rate + 2)
                               / (1000000 * frame->subbands)) - 10, 4, 16) & ~3;

        frame->allocation = SBC_AM_LOUDNESS;

        d = frame->blocks * ((frame->mode == SBC_MODE_DUAL_CHANNEL) + 1);
        frame->bitpool = (((avctx->bit_rate * frame->subbands * frame->blocks) / avctx->sample_rate)
                          - 4 * frame->subbands * avctx->ch_layout.nb_channels
                          - (frame->mode == SBC_MODE_JOINT_STEREO)*frame->subbands - 32 + d/2) / d;
        if (avctx->global_quality > 0)
            frame->bitpool = avctx->global_quality / FF_QP2LAMBDA;

        avctx->frame_size = 4*((frame->subbands >> 3) + 1) * 4*(frame->blocks >> 2);
    }

    for (int i = 0; avctx->codec->supported_samplerates[i]; i++)
        if (avctx->sample_rate == avctx->codec->supported_samplerates[i])
            frame->frequency = i;

    frame->channels = avctx->ch_layout.nb_channels;
    frame->codesize = frame->subbands * frame->blocks * avctx->ch_layout.nb_channels * 2;
    frame->crc_ctx = av_crc_get_table(AV_CRC_8_EBU);

    memset(&sbc->dsp.X, 0, sizeof(sbc->dsp.X));
    sbc->dsp.position = (SBC_X_BUFFER_SIZE - frame->subbands * 9) & ~7;
    sbc->dsp.increment = sbc->msbc ? 1 : 4;
    ff_sbcdsp_init(&sbc->dsp);

    return 0;
}

static int sbc_encode_frame(AVCodecContext *avctx, AVPacket *avpkt,
                            const AVFrame *av_frame, int *got_packet_ptr)
{
    SBCEncContext *sbc = avctx->priv_data;
    struct sbc_frame *frame = &sbc->frame;
    uint8_t joint = frame->mode == SBC_MODE_JOINT_STEREO;
    uint8_t dual  = frame->mode == SBC_MODE_DUAL_CHANNEL;
    int ret, j = 0;

    int frame_length = 4 + (4 * frame->subbands * frame->channels) / 8
                     + ((frame->blocks * frame->bitpool * (1 + dual)
                     + joint * frame->subbands) + 7) / 8;

    /* input must be large enough to encode a complete frame */
    if (av_frame->nb_samples * frame->channels * 2 < frame->codesize)
        return 0;

    if ((ret = ff_get_encode_buffer(avctx, avpkt, frame_length, 0)) < 0)
        return ret;

    /* Select the needed input data processing function and call it */
    if (frame->subbands == 8)
        sbc->dsp.position = sbc->dsp.sbc_enc_process_input_8s(
                sbc->dsp.position, av_frame->data[0], sbc->dsp.X,
                frame->subbands * frame->blocks, frame->channels);
    else
        sbc->dsp.position = sbc->dsp.sbc_enc_process_input_4s(
                sbc->dsp.position, av_frame->data[0], sbc->dsp.X,
                frame->subbands * frame->blocks, frame->channels);

    sbc_analyze_audio(&sbc->dsp, &sbc->frame);

    if (frame->mode == JOINT_STEREO)
        j = sbc->dsp.sbc_calc_scalefactors_j(frame->sb_sample_f,
                                             frame->scale_factor,
                                             frame->blocks,
                                             frame->subbands);
    else
        sbc->dsp.sbc_calc_scalefactors(frame->sb_sample_f,
                                       frame->scale_factor,
                                       frame->blocks,
                                       frame->channels,
                                       frame->subbands);
    emms_c();
    sbc_pack_frame(avpkt, frame, j, sbc->msbc);

    *got_packet_ptr = 1;
    return 0;
}

#define OFFSET(x) offsetof(SBCEncContext, x)
#define AE AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "sbc_delay", "set maximum algorithmic latency",
      OFFSET(max_delay), AV_OPT_TYPE_DURATION, {.i64 = 13000}, 1000,13000, AE },
    { "msbc",      "use mSBC mode (wideband speech mono SBC)",
      OFFSET(msbc),      AV_OPT_TYPE_BOOL,     {.i64 = 0},        0,    1, AE },
    FF_AVCTX_PROFILE_OPTION("msbc", NULL, AUDIO, FF_PROFILE_SBC_MSBC)
    { NULL },
};

static const AVClass sbc_class = {
    .class_name = "sbc encoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_sbc_encoder = {
    .p.name                = "sbc",
    .p.long_name           = NULL_IF_CONFIG_SMALL("SBC (low-complexity subband codec)"),
    .p.type                = AVMEDIA_TYPE_AUDIO,
    .p.id                  = AV_CODEC_ID_SBC,
    .p.capabilities        = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_SMALL_LAST_FRAME,
    .priv_data_size        = sizeof(SBCEncContext),
    .init                  = sbc_encode_init,
    FF_CODEC_ENCODE_CB(sbc_encode_frame),
    .caps_internal         = FF_CODEC_CAP_INIT_THREADSAFE,
#if FF_API_OLD_CHANNEL_LAYOUT
    .p.channel_layouts     = (const uint64_t[]) { AV_CH_LAYOUT_MONO,
                                                  AV_CH_LAYOUT_STEREO, 0},
#endif
    .p.ch_layouts          = (const AVChannelLayout[]) { AV_CHANNEL_LAYOUT_MONO,
                                                         AV_CHANNEL_LAYOUT_STEREO,
                                                         { 0 } },
    .p.sample_fmts         = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_S16,
                                                             AV_SAMPLE_FMT_NONE },
    .p.supported_samplerates = (const int[]) { 16000, 32000, 44100, 48000, 0 },
    .p.priv_class          = &sbc_class,
    .p.profiles            = NULL_IF_CONFIG_SMALL(ff_sbc_profiles),
};
