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
    char name[8];
    int bits;
    int planar;
    enum AVSampleFormat altform; ///< planar<->packed alternative form
} SampleFmtInfo;

/** this table gives more information about formats */
static const SampleFmtInfo sample_fmt_info[AV_SAMPLE_FMT_NB] = {
    [AV_SAMPLE_FMT_U8]   = { .name =   "u8", .bits =  8, .planar = 0, .altform = AV_SAMPLE_FMT_U8P  },
    [AV_SAMPLE_FMT_S16]  = { .name =  "s16", .bits = 16, .planar = 0, .altform = AV_SAMPLE_FMT_S16P },
    [AV_SAMPLE_FMT_S32]  = { .name =  "s32", .bits = 32, .planar = 0, .altform = AV_SAMPLE_FMT_S32P },
    [AV_SAMPLE_FMT_FLT]  = { .name =  "flt", .bits = 32, .planar = 0, .altform = AV_SAMPLE_FMT_FLTP },
    [AV_SAMPLE_FMT_DBL]  = { .name =  "dbl", .bits = 64, .planar = 0, .altform = AV_SAMPLE_FMT_DBLP },
    [AV_SAMPLE_FMT_U8P]  = { .name =  "u8p", .bits =  8, .planar = 1, .altform = AV_SAMPLE_FMT_U8   },
    [AV_SAMPLE_FMT_S16P] = { .name = "s16p", .bits = 16, .planar = 1, .altform = AV_SAMPLE_FMT_S16  },
    [AV_SAMPLE_FMT_S32P] = { .name = "s32p", .bits = 32, .planar = 1, .altform = AV_SAMPLE_FMT_S32  },
    [AV_SAMPLE_FMT_FLTP] = { .name = "fltp", .bits = 32, .planar = 1, .altform = AV_SAMPLE_FMT_FLT  },
    [AV_SAMPLE_FMT_DBLP] = { .name = "dblp", .bits = 64, .planar = 1, .altform = AV_SAMPLE_FMT_DBL  },
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

enum AVSampleFormat av_get_alt_sample_fmt(enum AVSampleFormat sample_fmt, int planar)
{
    if (sample_fmt < 0 || sample_fmt >= AV_SAMPLE_FMT_NB)
        return AV_SAMPLE_FMT_NONE;
    if (sample_fmt_info[sample_fmt].planar == planar)
        return sample_fmt;
    return sample_fmt_info[sample_fmt].altform;
}

enum AVSampleFormat av_get_packed_sample_fmt(enum AVSampleFormat sample_fmt)
{
    if (sample_fmt < 0 || sample_fmt >= AV_SAMPLE_FMT_NB)
        return AV_SAMPLE_FMT_NONE;
    if (sample_fmt_info[sample_fmt].planar)
        return sample_fmt_info[sample_fmt].altform;
    return sample_fmt;
}

enum AVSampleFormat av_get_planar_sample_fmt(enum AVSampleFormat sample_fmt)
{
    if (sample_fmt < 0 || sample_fmt >= AV_SAMPLE_FMT_NB)
        return AV_SAMPLE_FMT_NONE;
    if (sample_fmt_info[sample_fmt].planar)
        return sample_fmt;
    return sample_fmt_info[sample_fmt].altform;
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

int av_get_bytes_per_sample(enum AVSampleFormat sample_fmt)
{
     return sample_fmt < 0 || sample_fmt >= AV_SAMPLE_FMT_NB ?
        0 : sample_fmt_info[sample_fmt].bits >> 3;
}

#if FF_API_GET_BITS_PER_SAMPLE_FMT
int av_get_bits_per_sample_fmt(enum AVSampleFormat sample_fmt)
{
    return sample_fmt < 0 || sample_fmt >= AV_SAMPLE_FMT_NB ?
        0 : sample_fmt_info[sample_fmt].bits;
}
#endif

int av_sample_fmt_is_planar(enum AVSampleFormat sample_fmt)
{
     if (sample_fmt < 0 || sample_fmt >= AV_SAMPLE_FMT_NB)
         return 0;
     return sample_fmt_info[sample_fmt].planar;
}

int av_samples_get_buffer_size(int *linesize, int nb_channels, int nb_samples,
                               enum AVSampleFormat sample_fmt, int align)
{
    int line_size;
    int sample_size = av_get_bytes_per_sample(sample_fmt);
    int planar      = av_sample_fmt_is_planar(sample_fmt);

    /* validate parameter ranges */
    if (!sample_size || nb_samples <= 0 || nb_channels <= 0)
        return AVERROR(EINVAL);

    /* auto-select alignment if not specified */
    if (!align) {
        align = 1;
        nb_samples = FFALIGN(nb_samples, 32);
    }

    /* check for integer overflow */
    if (nb_channels > INT_MAX / align ||
        (int64_t)nb_channels * nb_samples > (INT_MAX - (align * nb_channels)) / sample_size)
        return AVERROR(EINVAL);

    line_size = planar ? FFALIGN(nb_samples * sample_size,               align) :
                         FFALIGN(nb_samples * sample_size * nb_channels, align);
    if (linesize)
        *linesize = line_size;

    return planar ? line_size * nb_channels : line_size;
}

int av_samples_fill_arrays(uint8_t **audio_data, int *linesize,
                           const uint8_t *buf, int nb_channels, int nb_samples,
                           enum AVSampleFormat sample_fmt, int align)
{
    int ch, planar, buf_size, line_size;

    planar   = av_sample_fmt_is_planar(sample_fmt);
    buf_size = av_samples_get_buffer_size(&line_size, nb_channels, nb_samples,
                                          sample_fmt, align);
    if (buf_size < 0)
        return buf_size;

    audio_data[0] = buf;
    for (ch = 1; planar && ch < nb_channels; ch++)
        audio_data[ch] = audio_data[ch-1] + line_size;

    if (linesize)
        *linesize = line_size;

    return 0;
}

int av_samples_alloc(uint8_t **audio_data, int *linesize, int nb_channels,
                     int nb_samples, enum AVSampleFormat sample_fmt, int align)
{
    uint8_t *buf;
    int size = av_samples_get_buffer_size(NULL, nb_channels, nb_samples,
                                          sample_fmt, align);
    if (size < 0)
        return size;

    buf = av_mallocz(size);
    if (!buf)
        return AVERROR(ENOMEM);

    size = av_samples_fill_arrays(audio_data, linesize, buf, nb_channels,
                                  nb_samples, sample_fmt, align);
    if (size < 0) {
        av_free(buf);
        return size;
    }
    return 0;
}

int av_samples_copy(uint8_t **dst, uint8_t * const *src, int dst_offset,
                    int src_offset, int nb_samples, int nb_channels,
                    enum AVSampleFormat sample_fmt)
{
    int planar      = av_sample_fmt_is_planar(sample_fmt);
    int planes      = planar ? nb_channels : 1;
    int block_align = av_get_bytes_per_sample(sample_fmt) * (planar ? 1 : nb_channels);
    int data_size   = nb_samples * block_align;
    int i;

    dst_offset *= block_align;
    src_offset *= block_align;

    for (i = 0; i < planes; i++)
        memcpy(dst[i] + dst_offset, src[i] + src_offset, data_size);

    return 0;
}

int av_samples_set_silence(uint8_t **audio_data, int offset, int nb_samples,
                           int nb_channels, enum AVSampleFormat sample_fmt)
{
    int planar      = av_sample_fmt_is_planar(sample_fmt);
    int planes      = planar ? nb_channels : 1;
    int block_align = av_get_bytes_per_sample(sample_fmt) * (planar ? 1 : nb_channels);
    int data_size   = nb_samples * block_align;
    int fill_char   = (sample_fmt == AV_SAMPLE_FMT_U8 ||
                     sample_fmt == AV_SAMPLE_FMT_U8P) ? 0x80 : 0x00;
    int i;

    offset *= block_align;

    for (i = 0; i < planes; i++)
        memset(audio_data[i] + offset, fill_char, data_size);

    return 0;
}
