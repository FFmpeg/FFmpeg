/*
 * Fixed-point MPEG audio decoder
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

#include "config.h"
#include "libavutil/samplefmt.h"

#define USE_FLOATS 0

#include "mpegaudio.h"

#define SHR(a,b)       ((a)>>(b))
/* WARNING: only correct for positive numbers */
#define FIXR_OLD(a)    ((int)((a) * FRAC_ONE + 0.5))
#define FIXR(a)        ((int)((a) * FRAC_ONE + 0.5))
#define FIXHR(a)       ((int)((a) * (1LL<<32) + 0.5))
#define MULH3(x, y, s) MULH((s)*(x), y)
#define MULLx(x, y, s) MULL(x,y,s)
#define RENAME(a)      a ## _fixed
#define OUT_FMT   AV_SAMPLE_FMT_S16
#define OUT_FMT_P AV_SAMPLE_FMT_S16P

#include "mpegaudiodec_template.c"

#if CONFIG_MP1_DECODER
AVCodec ff_mp1_decoder = {
    .name           = "mp1",
    .long_name      = NULL_IF_CONFIG_SMALL("MP1 (MPEG audio layer 1)"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_MP1,
    .priv_data_size = sizeof(MPADecodeContext),
    .init           = decode_init,
    .decode         = decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1,
    .flush          = flush,
    .sample_fmts    = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_S16P,
                                                      AV_SAMPLE_FMT_S16,
                                                      AV_SAMPLE_FMT_NONE },
};
#endif
#if CONFIG_MP2_DECODER
AVCodec ff_mp2_decoder = {
    .name           = "mp2",
    .long_name      = NULL_IF_CONFIG_SMALL("MP2 (MPEG audio layer 2)"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_MP2,
    .priv_data_size = sizeof(MPADecodeContext),
    .init           = decode_init,
    .decode         = decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1,
    .flush          = flush,
    .sample_fmts    = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_S16P,
                                                      AV_SAMPLE_FMT_S16,
                                                      AV_SAMPLE_FMT_NONE },
};
#endif
#if CONFIG_MP3_DECODER
AVCodec ff_mp3_decoder = {
    .name           = "mp3",
    .long_name      = NULL_IF_CONFIG_SMALL("MP3 (MPEG audio layer 3)"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_MP3,
    .priv_data_size = sizeof(MPADecodeContext),
    .init           = decode_init,
    .decode         = decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1,
    .flush          = flush,
    .sample_fmts    = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_S16P,
                                                      AV_SAMPLE_FMT_S16,
                                                      AV_SAMPLE_FMT_NONE },
};
#endif
#if CONFIG_MP3ADU_DECODER
AVCodec ff_mp3adu_decoder = {
    .name           = "mp3adu",
    .long_name      = NULL_IF_CONFIG_SMALL("ADU (Application Data Unit) MP3 (MPEG audio layer 3)"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_MP3ADU,
    .priv_data_size = sizeof(MPADecodeContext),
    .init           = decode_init,
    .decode         = decode_frame_adu,
    .capabilities   = AV_CODEC_CAP_DR1,
    .flush          = flush,
    .sample_fmts    = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_S16P,
                                                      AV_SAMPLE_FMT_S16,
                                                      AV_SAMPLE_FMT_NONE },
};
#endif
#if CONFIG_MP3ON4_DECODER
AVCodec ff_mp3on4_decoder = {
    .name           = "mp3on4",
    .long_name      = NULL_IF_CONFIG_SMALL("MP3onMP4"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_MP3ON4,
    .priv_data_size = sizeof(MP3On4DecodeContext),
    .init           = decode_init_mp3on4,
    .close          = decode_close_mp3on4,
    .decode         = decode_frame_mp3on4,
    .capabilities   = AV_CODEC_CAP_DR1,
    .flush          = flush_mp3on4,
    .sample_fmts    = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_S16P,
                                                      AV_SAMPLE_FMT_NONE },
};
#endif
