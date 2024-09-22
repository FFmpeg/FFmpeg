/*
 * Copyright (c) 2024 Michael Niedermayer <michael-ffmpeg@niedermayer.cc>
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
#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/cpu.h"
#include "libavutil/imgutils.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"

#include "libavcodec/bytestream.h"

#include "libswresample/swresample.h"

#define SWR_CH_MAX 32

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static const enum AVSampleFormat formats[] = {
    AV_SAMPLE_FMT_U8,
    AV_SAMPLE_FMT_U8P,
    AV_SAMPLE_FMT_S16,
    AV_SAMPLE_FMT_S16P,
    AV_SAMPLE_FMT_S32,
    AV_SAMPLE_FMT_S32P,
    AV_SAMPLE_FMT_FLT,
    AV_SAMPLE_FMT_FLTP,
    AV_SAMPLE_FMT_DBL,
    AV_SAMPLE_FMT_DBLP,
};

static const AVChannelLayout layouts[]={
    AV_CHANNEL_LAYOUT_MONO               ,
    AV_CHANNEL_LAYOUT_STEREO             ,
    AV_CHANNEL_LAYOUT_2_1                ,
    AV_CHANNEL_LAYOUT_SURROUND           ,
    AV_CHANNEL_LAYOUT_4POINT0            ,
    AV_CHANNEL_LAYOUT_2_2                ,
    AV_CHANNEL_LAYOUT_QUAD               ,
    AV_CHANNEL_LAYOUT_5POINT0            ,
    AV_CHANNEL_LAYOUT_5POINT1            ,
    AV_CHANNEL_LAYOUT_5POINT0_BACK       ,
    AV_CHANNEL_LAYOUT_5POINT1_BACK       ,
    AV_CHANNEL_LAYOUT_7POINT0            ,
    AV_CHANNEL_LAYOUT_7POINT1            ,
    AV_CHANNEL_LAYOUT_7POINT1_WIDE       ,
    AV_CHANNEL_LAYOUT_22POINT2           ,
    AV_CHANNEL_LAYOUT_5POINT1POINT2_BACK ,
};

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    struct SwrContext * swr= NULL;
    AVChannelLayout in_ch_layout = AV_CHANNEL_LAYOUT_MONO, out_ch_layout = AV_CHANNEL_LAYOUT_MONO;
    enum AVSampleFormat  in_sample_fmt = AV_SAMPLE_FMT_S16P;
    enum AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_S16P;
    int  in_sample_rate = 44100;
    int out_sample_rate = 44100;
    int in_ch_count, out_ch_count;
    char  in_layout_string[256];
    char out_layout_string[256];
    uint8_t * ain[SWR_CH_MAX];
    uint8_t *aout[SWR_CH_MAX];
    uint8_t *out_data;
    int in_sample_nb;
    int out_sample_nb = size;
    int count;
    int ret;

    if (size > 128) {
        GetByteContext gbc;
        int64_t flags64;

        size -= 128;
        bytestream2_init(&gbc, data + size, 128);
         in_sample_rate = bytestream2_get_le16(&gbc) + 1;
        out_sample_rate = bytestream2_get_le16(&gbc) + 1;
         in_sample_fmt  = formats[bytestream2_get_byte(&gbc) % FF_ARRAY_ELEMS(formats)];
        out_sample_fmt  = formats[bytestream2_get_byte(&gbc) % FF_ARRAY_ELEMS(formats)];
        av_channel_layout_copy(& in_ch_layout,  &layouts[bytestream2_get_byte(&gbc) % FF_ARRAY_ELEMS(layouts)]);
        av_channel_layout_copy(&out_ch_layout,  &layouts[bytestream2_get_byte(&gbc) % FF_ARRAY_ELEMS(layouts)]);

        out_sample_nb = bytestream2_get_le32(&gbc);

        flags64 = bytestream2_get_le64(&gbc);
        if (flags64 & 0x10)
            av_force_cpu_flags(0);
    }

     in_ch_count=  in_ch_layout.nb_channels;
    out_ch_count= out_ch_layout.nb_channels;
    av_channel_layout_describe(& in_ch_layout,  in_layout_string, sizeof( in_layout_string));
    av_channel_layout_describe(&out_ch_layout, out_layout_string, sizeof(out_layout_string));

    fprintf(stderr, "%s %d %s -> %s %d %s\n",
            av_get_sample_fmt_name( in_sample_fmt),  in_sample_rate,  in_layout_string,
            av_get_sample_fmt_name(out_sample_fmt), out_sample_rate, out_layout_string);

    if (swr_alloc_set_opts2(&swr, &out_ch_layout, out_sample_fmt, out_sample_rate,
                                  &in_ch_layout,   in_sample_fmt,  in_sample_rate,
                                  0, 0) < 0) {
        fprintf(stderr, "Failed swr_alloc_set_opts2()\n");
        goto end;
    }

    if (swr_init(swr) < 0) {
        fprintf(stderr, "Failed swr_init()\n");
        goto end;
    }

     in_sample_nb = size / (in_ch_count * av_get_bytes_per_sample(in_sample_fmt));
    out_sample_nb = out_sample_nb % (av_rescale(in_sample_nb, 2*out_sample_rate, in_sample_rate) + 1);

    if (in_sample_nb > 1000*1000 || out_sample_nb > 1000*1000)
        goto end;

    out_data = av_malloc(out_sample_nb * out_ch_count * av_get_bytes_per_sample(out_sample_fmt));
    if (!out_data)
        goto end;

    ret = av_samples_fill_arrays(ain , NULL,     data,  in_ch_count,  in_sample_nb,  in_sample_fmt, 1);
    if (ret < 0)
        goto end;
    ret = av_samples_fill_arrays(aout, NULL, out_data, out_ch_count, out_sample_nb, out_sample_fmt, 1);
    if (ret < 0)
        goto end;

    count = swr_convert(swr, aout, out_sample_nb, (const uint8_t **)ain, in_sample_nb);

    av_freep(&out_data);

end:
    swr_free(&swr);

    return 0;
}
