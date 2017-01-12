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

#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include "libavutil/mem.h"
#include "libavutil/audio_fifo.c"

#define MAX_CHANNELS    32


typedef struct TestStruct {
    const enum AVSampleFormat format;
    const int nb_ch;
    void const *data_planes[MAX_CHANNELS];
    const int nb_samples_pch;
} TestStruct;

static const uint8_t data_U8 [] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11                        };
static const int16_t data_S16[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11                        };
static const float   data_FLT[] = {0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0, 11.0};

static const TestStruct test_struct[] = {
    {.format = AV_SAMPLE_FMT_U8   , .nb_ch = 1, .data_planes = {data_U8 ,             }, .nb_samples_pch = 12},
    {.format = AV_SAMPLE_FMT_U8P  , .nb_ch = 2, .data_planes = {data_U8 , data_U8 +6, }, .nb_samples_pch = 6 },
    {.format = AV_SAMPLE_FMT_S16  , .nb_ch = 1, .data_planes = {data_S16,             }, .nb_samples_pch = 12},
    {.format = AV_SAMPLE_FMT_S16P , .nb_ch = 2, .data_planes = {data_S16, data_S16+6, }, .nb_samples_pch = 6 },
    {.format = AV_SAMPLE_FMT_FLT  , .nb_ch = 1, .data_planes = {data_FLT,             }, .nb_samples_pch = 12},
    {.format = AV_SAMPLE_FMT_FLTP , .nb_ch = 2, .data_planes = {data_FLT, data_FLT+6, }, .nb_samples_pch = 6 }
};

static void free_data_planes(AVAudioFifo *afifo, void **output_data)
{
    int i;
    for (i = 0; i < afifo->nb_buffers; ++i){
        av_freep(&output_data[i]);
    }
    av_freep(&output_data);
}

static void ERROR(const char *str)
{
    fprintf(stderr, "%s\n", str);
    exit(1);
}

static void print_audio_bytes(const TestStruct *test_sample, void **data_planes, int nb_samples)
{
    int p, b, f;
    int byte_offset      = av_get_bytes_per_sample(test_sample->format);
    int buffers          = av_sample_fmt_is_planar(test_sample->format)
                                         ? test_sample->nb_ch : 1;
    int line_size        = (buffers > 1) ? nb_samples * byte_offset
                                         : nb_samples * byte_offset * test_sample->nb_ch;
    for (p = 0; p < buffers; ++p){
        for(b = 0; b < line_size; b+=byte_offset){
            for (f = 0; f < byte_offset; f++){
                int order = !HAVE_BIGENDIAN ? (byte_offset - f - 1) : f;
                printf("%02x", *((uint8_t*)data_planes[p] + b + order));
            }
            putchar(' ');
        }
        putchar('\n');
    }
}

static int read_samples_from_audio_fifo(AVAudioFifo* afifo, void ***output, int nb_samples)
{
    int i;
    int samples        = FFMIN(nb_samples, afifo->nb_samples);
    int tot_elements   = !av_sample_fmt_is_planar(afifo->sample_fmt)
                         ? samples : afifo->channels * samples;
    void **data_planes = av_malloc_array(afifo->nb_buffers, sizeof(void*));
    if (!data_planes)
        ERROR("failed to allocate memory!");
    if (*output)
        free_data_planes(afifo, *output);
    *output            = data_planes;

    for (i = 0; i < afifo->nb_buffers; ++i){
        data_planes[i] = av_malloc_array(tot_elements, afifo->sample_size);
        if (!data_planes[i])
            ERROR("failed to allocate memory!");
    }

    return av_audio_fifo_read(afifo, *output, nb_samples);
}

static int write_samples_to_audio_fifo(AVAudioFifo* afifo, const TestStruct *test_sample,
                                       int nb_samples, int offset)
{
    int offset_size, i;
    void *data_planes[MAX_CHANNELS];

    if(nb_samples > test_sample->nb_samples_pch - offset){
        return 0;
    }
    if(offset >= test_sample->nb_samples_pch){
        return 0;
    }
    offset_size  = offset * afifo->sample_size;

    for (i = 0; i < afifo->nb_buffers ; ++i){
        data_planes[i] = (uint8_t*)test_sample->data_planes[i] + offset_size;
    }

    return av_audio_fifo_write(afifo, data_planes, nb_samples);
}

static void test_function(const TestStruct *test_sample)
{
    int ret, i;
    void **output_data  = NULL;
    AVAudioFifo *afifo  = av_audio_fifo_alloc(test_sample->format, test_sample->nb_ch,
                                            test_sample->nb_samples_pch);
    if (!afifo) {
        ERROR("ERROR: av_audio_fifo_alloc returned NULL!");
    }
    ret = write_samples_to_audio_fifo(afifo, test_sample, test_sample->nb_samples_pch, 0);
    if (ret < 0){
        ERROR("ERROR: av_audio_fifo_write failed!");
    }
    printf("written: %d\n", ret);

    ret = write_samples_to_audio_fifo(afifo, test_sample, test_sample->nb_samples_pch, 0);
    if (ret < 0){
        ERROR("ERROR: av_audio_fifo_write failed!");
    }
    printf("written: %d\n", ret);
    printf("remaining samples in audio_fifo: %d\n\n", av_audio_fifo_size(afifo));

    ret = read_samples_from_audio_fifo(afifo, &output_data, test_sample->nb_samples_pch);
    if (ret < 0){
        ERROR("ERROR: av_audio_fifo_read failed!");
    }
    printf("read: %d\n", ret);
    print_audio_bytes(test_sample, output_data, ret);
    printf("remaining samples in audio_fifo: %d\n\n", av_audio_fifo_size(afifo));

    /* test av_audio_fifo_peek */
    ret = av_audio_fifo_peek(afifo, output_data, afifo->nb_samples);
    if (ret < 0){
        ERROR("ERROR: av_audio_fifo_peek failed!");
    }
    printf("peek:\n");
    print_audio_bytes(test_sample, output_data, ret);
    printf("\n");

    /* test av_audio_fifo_peek_at */
    printf("peek_at:\n");
    for (i = 0; i < afifo->nb_samples; ++i){
        ret = av_audio_fifo_peek_at(afifo, output_data, 1, i);
        if (ret < 0){
            ERROR("ERROR: av_audio_fifo_peek_at failed!");
        }
        printf("%d:\n", i);
        print_audio_bytes(test_sample, output_data, ret);
    }
    printf("\n");

    /* test av_audio_fifo_drain */
    ret = av_audio_fifo_drain(afifo, afifo->nb_samples);
    if (ret < 0){
        ERROR("ERROR: av_audio_fifo_drain failed!");
    }
    if (afifo->nb_samples){
        ERROR("drain failed to flush all samples in audio_fifo!");
    }

    /* deallocate */
    free_data_planes(afifo, output_data);
    av_audio_fifo_free(afifo);
}

int main(void)
{
    int t, tests = sizeof(test_struct)/sizeof(test_struct[0]);

    for (t = 0; t < tests; ++t){
        printf("\nTEST: %d\n\n", t+1);
        test_function(&test_struct[t]);
    }
    return 0;
}
