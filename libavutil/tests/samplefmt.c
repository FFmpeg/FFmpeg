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

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "libavutil/error.h"
#include "libavutil/mem.h"
#include "libavutil/samplefmt.h"

int main(void)
{
    char buf[64];

    /* av_get_sample_fmt_name and av_get_sample_fmt round-trip */
    printf("Testing name/format round-trip\n");
    for (int i = 0; i < AV_SAMPLE_FMT_NB; i++) {
        const char *name = av_get_sample_fmt_name(i);
        enum AVSampleFormat fmt = av_get_sample_fmt(name);
        printf("%2d: name=%-5s roundtrip=%s\n", i, name, fmt == i ? "OK" : "FAIL");
    }

    /* boundary: NONE and out-of-range */
    printf("NONE name: %s\n", av_get_sample_fmt_name(AV_SAMPLE_FMT_NONE) == NULL ? "(null)" : "?");
    printf("NB name: %s\n", av_get_sample_fmt_name(AV_SAMPLE_FMT_NB) == NULL ? "(null)" : "?");
    printf("unknown: %d\n", av_get_sample_fmt("nonexistent"));

    /* av_get_bytes_per_sample */
    printf("\nTesting av_get_bytes_per_sample()\n");
    for (int i = 0; i < AV_SAMPLE_FMT_NB; i++)
        printf("%s: %d\n", av_get_sample_fmt_name(i), av_get_bytes_per_sample(i));
    printf("NONE: %d\n", av_get_bytes_per_sample(AV_SAMPLE_FMT_NONE));

    /* av_sample_fmt_is_planar */
    printf("\nTesting av_sample_fmt_is_planar()\n");
    for (int i = 0; i < AV_SAMPLE_FMT_NB; i++)
        printf("%s: %d\n", av_get_sample_fmt_name(i), av_sample_fmt_is_planar(i));
    printf("NONE: %d\n", av_sample_fmt_is_planar(AV_SAMPLE_FMT_NONE));

    /* av_get_packed_sample_fmt and av_get_planar_sample_fmt */
    printf("\nTesting packed/planar conversions\n");
    for (int i = 0; i < AV_SAMPLE_FMT_NB; i++) {
        enum AVSampleFormat packed = av_get_packed_sample_fmt(i);
        enum AVSampleFormat planar = av_get_planar_sample_fmt(i);
        printf("%s: packed=%-4s planar=%s\n",
               av_get_sample_fmt_name(i),
               av_get_sample_fmt_name(packed),
               av_get_sample_fmt_name(planar));
    }

    /* av_get_alt_sample_fmt */
    printf("\nTesting av_get_alt_sample_fmt()\n");
    for (int i = 0; i < AV_SAMPLE_FMT_NB; i++) {
        enum AVSampleFormat alt_packed = av_get_alt_sample_fmt(i, 0);
        enum AVSampleFormat alt_planar = av_get_alt_sample_fmt(i, 1);
        printf("%s: alt_packed=%-4s alt_planar=%s\n",
               av_get_sample_fmt_name(i),
               av_get_sample_fmt_name(alt_packed),
               av_get_sample_fmt_name(alt_planar));
    }

    /* av_get_sample_fmt_string */
    printf("\nTesting av_get_sample_fmt_string()\n");
    av_get_sample_fmt_string(buf, sizeof(buf), -1);
    printf("header: %s\n", buf);
    for (int i = 0; i < AV_SAMPLE_FMT_NB; i++) {
        av_get_sample_fmt_string(buf, sizeof(buf), i);
        printf("%s\n", buf);
    }

    /* av_samples_get_buffer_size */
    printf("\nTesting av_samples_get_buffer_size()\n");
    {
        int linesize;
        printf("2ch 1024smp s16: %d\n",
               av_samples_get_buffer_size(NULL, 2, 1024, AV_SAMPLE_FMT_S16, 1));
        printf("2ch 1024smp s16p: %d\n",
               av_samples_get_buffer_size(NULL, 2, 1024, AV_SAMPLE_FMT_S16P, 1));
        printf("6ch 512smp s32: %d\n",
               av_samples_get_buffer_size(NULL, 6, 512, AV_SAMPLE_FMT_S32, 1));
        av_samples_get_buffer_size(&linesize, 2, 1024, AV_SAMPLE_FMT_S16, 0);
        printf("linesize (2ch 1024smp s16 align=0): %d\n", linesize);
        printf("0ch error: %d\n",
               av_samples_get_buffer_size(NULL, 0, 1024, AV_SAMPLE_FMT_S16, 1) < 0);
    }

    /* av_samples_alloc and av_samples_fill_arrays */
    printf("\nTesting av_samples_alloc()\n");
    {
        uint8_t *data[8] = { 0 };
        int linesize, ret;

        ret = av_samples_alloc(data, &linesize, 2, 1024, AV_SAMPLE_FMT_S16, 0);
        printf("alloc 2ch s16: ret=%d linesize=%d data[0]=%s\n",
               ret > 0, linesize, data[0] ? "set" : "null");
        av_freep(&data[0]);

        ret = av_samples_alloc(data, &linesize, 2, 1024, AV_SAMPLE_FMT_S16P, 0);
        printf("alloc 2ch s16p: ret=%d linesize=%d data[0]=%s data[1]=%s\n",
               ret > 0, linesize,
               data[0] ? "set" : "null", data[1] ? "set" : "null");
        av_freep(&data[0]);
    }

    /* av_samples_alloc_array_and_samples */
    printf("\nTesting av_samples_alloc_array_and_samples()\n");
    {
        uint8_t **data = NULL;
        int linesize, ret;

        ret = av_samples_alloc_array_and_samples(&data, &linesize, 2, 1024,
                                                  AV_SAMPLE_FMT_S16P, 0);
        printf("alloc_array 2ch s16p: ret=%d linesize=%d data[0]=%s data[1]=%s\n",
               ret > 0, linesize,
               data[0] ? "set" : "null", data[1] ? "set" : "null");
        if (data)
            av_freep(&data[0]);
        av_freep(&data);
    }

    /* av_samples_copy */
    printf("\nTesting av_samples_copy()\n");
    {
        uint8_t *src[1] = { 0 }, *dst[1] = { 0 };
        int linesize;

        av_samples_alloc(src, &linesize, 1, 4, AV_SAMPLE_FMT_S16, 1);
        av_samples_alloc(dst, &linesize, 1, 4, AV_SAMPLE_FMT_S16, 1);
        if (src[0] && dst[0]) {
            memset(src[0], 0xAB, 8);
            av_samples_copy(dst, src, 0, 0, 4, 1, AV_SAMPLE_FMT_S16);
            printf("copy: %s\n", memcmp(src[0], dst[0], 8) == 0 ? "OK" : "FAIL");
        }
        av_freep(&src[0]);
        av_freep(&dst[0]);
    }

    /* OOM paths via av_max_alloc */
    printf("\nTesting OOM paths\n");
    {
        uint8_t *data[8] = { 0 };
        uint8_t **array_data = NULL;
        int linesize, ret;

        av_max_alloc(1);

        ret = av_samples_alloc(data, &linesize, 2, 1024, AV_SAMPLE_FMT_S16, 0);
        printf("alloc OOM: ret=%d data[0]=%s\n",
               ret == AVERROR(ENOMEM), data[0] ? "set" : "null");

        ret = av_samples_alloc_array_and_samples(&array_data, &linesize, 2, 1024,
                                                  AV_SAMPLE_FMT_S16P, 0);
        printf("alloc_array OOM: ret=%d data=%s\n",
               ret == AVERROR(ENOMEM), array_data ? "set" : "null");

        av_max_alloc(INT_MAX);
    }

    /* av_samples_set_silence */
    printf("\nTesting av_samples_set_silence()\n");
    {
        uint8_t *data[1] = { 0 };
        int linesize;

        av_samples_alloc(data, &linesize, 1, 4, AV_SAMPLE_FMT_S16, 1);
        if (data[0]) {
            memset(data[0], 0xFF, 8);
            av_samples_set_silence(data, 0, 4, 1, AV_SAMPLE_FMT_S16);
            /* silence for s16 is zero */
            int silent = 1;
            for (int i = 0; i < 8; i++)
                if (data[0][i] != 0) silent = 0;
            printf("silence s16: %s\n", silent ? "OK" : "FAIL");
        }
        av_freep(&data[0]);

        av_samples_alloc(data, &linesize, 1, 4, AV_SAMPLE_FMT_U8, 1);
        if (data[0]) {
            memset(data[0], 0xFF, 4);
            av_samples_set_silence(data, 0, 4, 1, AV_SAMPLE_FMT_U8);
            /* silence for u8 is 0x80 */
            int silent = 1;
            for (int i = 0; i < 4; i++)
                if (data[0][i] != 0x80) silent = 0;
            printf("silence u8: %s\n", silent ? "OK" : "FAIL");
        }
        av_freep(&data[0]);
    }

    return 0;
}
