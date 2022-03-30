/*
 * AC-3 Audio Decoder
 * This code was developed as part of Google Summer of Code 2006.
 * E-AC-3 support was added as part of Google Summer of Code 2007.
 *
 * Copyright (c) 2006 Kartikey Mahendra BHATT (bhattkm at gmail dot com)
 * Copyright (c) 2007-2008 Bartlomiej Wolowiec <bartek.wolowiec@gmail.com>
 * Copyright (c) 2007 Justin Ruggles <justin.ruggles@gmail.com>
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
 * Upmix delay samples from stereo to original channel layout.
 */

#include "config_components.h"

#include "ac3dec.h"
#include "codec_internal.h"
#include "eac3dec.c"
#include "ac3dec.c"

static const AVOption options[] = {
    { "cons_noisegen", "enable consistent noise generation", OFFSET(consistent_noise_generation), AV_OPT_TYPE_BOOL, {.i64 = 0 }, 0, 1, PAR },
    { "drc_scale", "percentage of dynamic range compression to apply", OFFSET(drc_scale), AV_OPT_TYPE_FLOAT, {.dbl = 1.0}, 0.0, 6.0, PAR },
    { "heavy_compr", "enable heavy dynamic range compression", OFFSET(heavy_compression), AV_OPT_TYPE_BOOL, {.i64 = 0 }, 0, 1, PAR },
    { "target_level", "target level in -dBFS (0 not applied)", OFFSET(target_level), AV_OPT_TYPE_INT, {.i64 = 0 }, -31, 0, PAR },

{"dmix_mode", "Preferred Stereo Downmix Mode", OFFSET(preferred_stereo_downmix), AV_OPT_TYPE_INT, {.i64 = -1 }, -1, 2, 0, "dmix_mode"},
{"ltrt_cmixlev",   "Lt/Rt Center Mix Level",   OFFSET(ltrt_center_mix_level),    AV_OPT_TYPE_FLOAT, {.dbl = -1.0 }, -1.0, 2.0, 0},
{"ltrt_surmixlev", "Lt/Rt Surround Mix Level", OFFSET(ltrt_surround_mix_level),  AV_OPT_TYPE_FLOAT, {.dbl = -1.0 }, -1.0, 2.0, 0},
{"loro_cmixlev",   "Lo/Ro Center Mix Level",   OFFSET(loro_center_mix_level),    AV_OPT_TYPE_FLOAT, {.dbl = -1.0 }, -1.0, 2.0, 0},
{"loro_surmixlev", "Lo/Ro Surround Mix Level", OFFSET(loro_surround_mix_level),  AV_OPT_TYPE_FLOAT, {.dbl = -1.0 }, -1.0, 2.0, 0},

    { "downmix", "Request a specific channel layout from the decoder", OFFSET(downmix_layout), AV_OPT_TYPE_CHLAYOUT, {.str = NULL}, .flags = PAR },

    { NULL},
};

static const AVClass ac3_eac3_decoder_class = {
    .class_name = "(E-)AC3 decoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_ac3_decoder = {
    .p.name         = "ac3",
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_AC3,
    .priv_data_size = sizeof (AC3DecodeContext),
    .init           = ac3_decode_init,
    .close          = ac3_decode_end,
    FF_CODEC_DECODE_CB(ac3_decode_frame),
    .p.capabilities = AV_CODEC_CAP_CHANNEL_CONF |
                      AV_CODEC_CAP_DR1,
    .p.long_name    = NULL_IF_CONFIG_SMALL("ATSC A/52A (AC-3)"),
    .p.sample_fmts  = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_FLTP,
                                                      AV_SAMPLE_FMT_NONE },
    .p.priv_class   = &ac3_eac3_decoder_class,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP,
};

#if CONFIG_EAC3_DECODER
const FFCodec ff_eac3_decoder = {
    .p.name         = "eac3",
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_EAC3,
    .priv_data_size = sizeof (AC3DecodeContext),
    .init           = ac3_decode_init,
    .close          = ac3_decode_end,
    FF_CODEC_DECODE_CB(ac3_decode_frame),
    .p.capabilities = AV_CODEC_CAP_CHANNEL_CONF |
                      AV_CODEC_CAP_DR1,
    .p.long_name    = NULL_IF_CONFIG_SMALL("ATSC A/52B (AC-3, E-AC-3)"),
    .p.sample_fmts  = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_FLTP,
                                                      AV_SAMPLE_FMT_NONE },
    .p.priv_class   = &ac3_eac3_decoder_class,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP,
};
#endif
