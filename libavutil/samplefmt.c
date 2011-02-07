/*
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

#include "samplefmt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct SampleFmtInfo {
    const char *name;
    int bits;
} SampleFmtInfo;

/** this table gives more information about formats */
static const SampleFmtInfo sample_fmt_info[AV_SAMPLE_FMT_NB] = {
    [AV_SAMPLE_FMT_U8]  = { .name = "u8",  .bits = 8 },
    [AV_SAMPLE_FMT_S16] = { .name = "s16", .bits = 16 },
    [AV_SAMPLE_FMT_S32] = { .name = "s32", .bits = 32 },
    [AV_SAMPLE_FMT_FLT] = { .name = "flt", .bits = 32 },
    [AV_SAMPLE_FMT_DBL] = { .name = "dbl", .bits = 64 },
};

const char *av_get_sample_fmt_name(enum AVSampleFormat sample_fmt)
{
    if (sample_fmt < 0 || sample_fmt >= AV_SAMPLE_FMT_NB)
        return NULL;
    return sample_fmt_info[sample_fmt].name;
}

enum AVSampleFormat av_get_sample_fmt(const char *name)
{
    int i;

    for (i = 0; i < AV_SAMPLE_FMT_NB; i++)
        if (!strcmp(sample_fmt_info[i].name, name))
            return i;
    return AV_SAMPLE_FMT_NONE;
}

char *av_get_sample_fmt_string (char *buf, int buf_size, enum AVSampleFormat sample_fmt)
{
    /* print header */
    if (sample_fmt < 0)
        snprintf(buf, buf_size, "name  " " depth");
    else if (sample_fmt < AV_SAMPLE_FMT_NB) {
        SampleFmtInfo info = sample_fmt_info[sample_fmt];
        snprintf (buf, buf_size, "%-6s" "   %2d ", info.name, info.bits);
    }

    return buf;
}

int av_get_bits_per_sample_fmt(enum AVSampleFormat sample_fmt)
{
    return sample_fmt < 0 || sample_fmt >= AV_SAMPLE_FMT_NB ?
        0 : sample_fmt_info[sample_fmt].bits;
}

int av_samples_fill_arrays(uint8_t *pointers[8], int linesizes[8],
                           uint8_t *buf, int nb_channels, int nb_samples,
                           enum AVSampleFormat sample_fmt, int planar, int align)
{
    int i, step_size = 0;
    int sample_size = av_get_bits_per_sample_fmt(sample_fmt) >> 3;
    int channel_step = planar ? FFALIGN(nb_samples*sample_size, align) : sample_size;

    if(nb_channels * (uint64_t)nb_samples * sample_size >= INT_MAX - align*(uint64_t)nb_channels)
        return AVERROR(EINVAL);

    if (pointers) {
        pointers[0] = buf;
        for (i = 0; i < nb_channels; i++) {
            pointers[i] = buf + step_size;
            step_size += channel_step;
        }
        memset(&pointers[nb_channels], 0, (8-nb_channels) * sizeof(pointers[0]));
    }

    if (linesizes) {
        linesizes[0] = planar ?  sample_size : nb_channels*sample_size;
        memset(&linesizes[1], 0, (8-1) * sizeof(linesizes[0]));
    }

    return planar ? channel_step * nb_channels : FFALIGN(nb_channels*sample_size*nb_samples, align);
}

int av_samples_alloc(uint8_t *pointers[8], int linesizes[8],
                     int nb_samples, int nb_channels,
                     enum AVSampleFormat sample_fmt, int planar,
                     int align)
{
    uint8_t *buf;
    int size = av_samples_fill_arrays(NULL, NULL,
                                      NULL, nb_channels, nb_samples,
                                      sample_fmt, planar, align);

    buf = av_mallocz(size);
    if (!buf)
        return AVERROR(ENOMEM);

    return av_samples_fill_arrays(pointers, linesizes,
                                  buf, nb_channels, nb_samples,
                                  sample_fmt, planar, align);
}
