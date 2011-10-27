/*
 * samplerate conversion for both audio and video
 * Copyright (c) 2000 Fabrice Bellard
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
 * samplerate conversion for both audio and video
 */

#include "avcodec.h"
#include "audioconvert.h"
#include "libavutil/opt.h"
#include "libavutil/samplefmt.h"

#define MAX_CHANNELS 8

struct AVResampleContext;

static const char *context_to_name(void *ptr)
{
    return "audioresample";
}

static const AVOption options[] = {{NULL}};
static const AVClass audioresample_context_class = {
    "ReSampleContext", context_to_name, options, LIBAVUTIL_VERSION_INT
};

struct ReSampleContext {
    struct AVResampleContext *resample_context;
    short *temp[MAX_CHANNELS];
    int temp_len;
    float ratio;
    /* channel convert */
    int input_channels, output_channels, filter_channels;
    AVAudioConvert *convert_ctx[2];
    enum AVSampleFormat sample_fmt[2]; ///< input and output sample format
    unsigned sample_size[2];           ///< size of one sample in sample_fmt
    short *buffer[2];                  ///< buffers used for conversion to S16
    unsigned buffer_size[2];           ///< sizes of allocated buffers
};

/* n1: number of samples */
static void stereo_to_mono(short *output, short *input, int n1)
{
    short *p, *q;
    int n = n1;

    p = input;
    q = output;
    while (n >= 4) {
        q[0] = (p[0] + p[1]) >> 1;
        q[1] = (p[2] + p[3]) >> 1;
        q[2] = (p[4] + p[5]) >> 1;
        q[3] = (p[6] + p[7]) >> 1;
        q += 4;
        p += 8;
        n -= 4;
    }
    while (n > 0) {
        q[0] = (p[0] + p[1]) >> 1;
        q++;
        p += 2;
        n--;
    }
}

/* n1: number of samples */
static void mono_to_stereo(short *output, short *input, int n1)
{
    short *p, *q;
    int n = n1;
    int v;

    p = input;
    q = output;
    while (n >= 4) {
        v = p[0]; q[0] = v; q[1] = v;
        v = p[1]; q[2] = v; q[3] = v;
        v = p[2]; q[4] = v; q[5] = v;
        v = p[3]; q[6] = v; q[7] = v;
        q += 8;
        p += 4;
        n -= 4;
    }
    while (n > 0) {
        v = p[0]; q[0] = v; q[1] = v;
        q += 2;
        p += 1;
        n--;
    }
}

/*
5.1 to stereo input: [fl, fr, c, lfe, rl, rr]
- Left = front_left + rear_gain * rear_left + center_gain * center
- Right = front_right + rear_gain * rear_right + center_gain * center
Where rear_gain is usually around 0.5-1.0 and
      center_gain is almost always 0.7 (-3 dB)
*/
static void surround_to_stereo(short **output, short *input, int channels, int samples)
{
    int i;
    short l, r;

    for (i = 0; i < samples; i++) {
        int fl,fr,c,rl,rr;
        fl = input[0];
        fr = input[1];
        c = input[2];
        // lfe = input[3];
        rl = input[4];
        rr = input[5];

        l = av_clip_int16(fl + (0.5 * rl) + (0.7 * c));
        r = av_clip_int16(fr + (0.5 * rr) + (0.7 * c));

        /* output l & r. */
        *output[0]++ = l;
        *output[1]++ = r;

        /* increment input. */
        input += channels;
    }
}

static void deinterleave(short **output, short *input, int channels, int samples)
{
    int i, j;

    for (i = 0; i < samples; i++) {
        for (j = 0; j < channels; j++) {
            *output[j]++ = *input++;
        }
    }
}

static void interleave(short *output, short **input, int channels, int samples)
{
    int i, j;

    for (i = 0; i < samples; i++) {
        for (j = 0; j < channels; j++) {
            *output++ = *input[j]++;
        }
    }
}

static void ac3_5p1_mux(short *output, short *input1, short *input2, int n)
{
    int i;
    short l, r;

    for (i = 0; i < n; i++) {
        l = *input1++;
        r = *input2++;
        *output++ = l;                  /* left */
        *output++ = (l / 2) + (r / 2);  /* center */
        *output++ = r;                  /* right */
        *output++ = 0;                  /* left surround */
        *output++ = 0;                  /* right surroud */
        *output++ = 0;                  /* low freq */
    }
}

#define SUPPORT_RESAMPLE(ch1, ch2, ch3, ch4, ch5, ch6, ch7, ch8) \
    ch8<<7 | ch7<<6 | ch6<<5 | ch5<<4 | ch4<<3 | ch3<<2 | ch2<<1 | ch1<<0

static const uint8_t supported_resampling[MAX_CHANNELS] = {
    // output ch:    1  2  3  4  5  6  7  8
    SUPPORT_RESAMPLE(1, 1, 0, 0, 0, 0, 0, 0), // 1 input channel
    SUPPORT_RESAMPLE(1, 1, 0, 0, 0, 1, 0, 0), // 2 input channels
    SUPPORT_RESAMPLE(0, 0, 1, 0, 0, 0, 0, 0), // 3 input channels
    SUPPORT_RESAMPLE(0, 0, 0, 1, 0, 0, 0, 0), // 4 input channels
    SUPPORT_RESAMPLE(0, 0, 0, 0, 1, 0, 0, 0), // 5 input channels
    SUPPORT_RESAMPLE(0, 1, 0, 0, 0, 1, 0, 0), // 6 input channels
    SUPPORT_RESAMPLE(0, 0, 0, 0, 0, 0, 1, 0), // 7 input channels
    SUPPORT_RESAMPLE(0, 0, 0, 0, 0, 0, 0, 1), // 8 input channels
};

ReSampleContext *av_audio_resample_init(int output_channels, int input_channels,
                                        int output_rate, int input_rate,
                                        enum AVSampleFormat sample_fmt_out,
                                        enum AVSampleFormat sample_fmt_in,
                                        int filter_length, int log2_phase_count,
                                        int linear, double cutoff)
{
    ReSampleContext *s;

    if (input_channels > MAX_CHANNELS) {
        av_log(NULL, AV_LOG_ERROR,
               "Resampling with input channels greater than %d is unsupported.\n",
               MAX_CHANNELS);
        return NULL;
    }
    if (!(supported_resampling[input_channels-1] & (1<<(output_channels-1)))) {
        int i;
        av_log(NULL, AV_LOG_ERROR, "Unsupported audio resampling. Allowed "
               "output channels for %d input channel%s", input_channels,
               input_channels > 1 ? "s:" : ":");
        for (i = 0; i < MAX_CHANNELS; i++)
            if (supported_resampling[input_channels-1] & (1<<i))
                av_log(NULL, AV_LOG_ERROR, " %d", i + 1);
        av_log(NULL, AV_LOG_ERROR, "\n");
        return NULL;
    }

    s = av_mallocz(sizeof(ReSampleContext));
    if (!s) {
        av_log(NULL, AV_LOG_ERROR, "Can't allocate memory for resample context.\n");
        return NULL;
    }

    s->ratio = (float)output_rate / (float)input_rate;

    s->input_channels = input_channels;
    s->output_channels = output_channels;

    s->filter_channels = s->input_channels;
    if (s->output_channels < s->filter_channels)
        s->filter_channels = s->output_channels;

    s->sample_fmt[0]  = sample_fmt_in;
    s->sample_fmt[1]  = sample_fmt_out;
    s->sample_size[0] = av_get_bytes_per_sample(s->sample_fmt[0]);
    s->sample_size[1] = av_get_bytes_per_sample(s->sample_fmt[1]);

    if (s->sample_fmt[0] != AV_SAMPLE_FMT_S16) {
        if (!(s->convert_ctx[0] = av_audio_convert_alloc(AV_SAMPLE_FMT_S16, 1,
                                                         s->sample_fmt[0], 1, NULL, 0))) {
            av_log(s, AV_LOG_ERROR,
                   "Cannot convert %s sample format to s16 sample format\n",
                   av_get_sample_fmt_name(s->sample_fmt[0]));
            av_free(s);
            return NULL;
        }
    }

    if (s->sample_fmt[1] != AV_SAMPLE_FMT_S16) {
        if (!(s->convert_ctx[1] = av_audio_convert_alloc(s->sample_fmt[1], 1,
                                                         AV_SAMPLE_FMT_S16, 1, NULL, 0))) {
            av_log(s, AV_LOG_ERROR,
                   "Cannot convert s16 sample format to %s sample format\n",
                   av_get_sample_fmt_name(s->sample_fmt[1]));
            av_audio_convert_free(s->convert_ctx[0]);
            av_free(s);
            return NULL;
        }
    }

    s->resample_context = av_resample_init(output_rate, input_rate,
                                           filter_length, log2_phase_count,
                                           linear, cutoff);

    *(const AVClass**)s->resample_context = &audioresample_context_class;

    return s;
}

/* resample audio. 'nb_samples' is the number of input samples */
/* XXX: optimize it ! */
int audio_resample(ReSampleContext *s, short *output, short *input, int nb_samples)
{
    int i, nb_samples1;
    short *bufin[MAX_CHANNELS];
    short *bufout[MAX_CHANNELS];
    short *buftmp2[MAX_CHANNELS], *buftmp3[MAX_CHANNELS];
    short *output_bak = NULL;
    int lenout;

    if (s->input_channels == s->output_channels && s->ratio == 1.0 && 0) {
        /* nothing to do */
        memcpy(output, input, nb_samples * s->input_channels * sizeof(short));
        return nb_samples;
    }

    if (s->sample_fmt[0] != AV_SAMPLE_FMT_S16) {
        int istride[1] = { s->sample_size[0] };
        int ostride[1] = { 2 };
        const void *ibuf[1] = { input };
        void       *obuf[1];
        unsigned input_size = nb_samples * s->input_channels * 2;

        if (!s->buffer_size[0] || s->buffer_size[0] < input_size) {
            av_free(s->buffer[0]);
            s->buffer_size[0] = input_size;
            s->buffer[0] = av_malloc(s->buffer_size[0]);
            if (!s->buffer[0]) {
                av_log(s->resample_context, AV_LOG_ERROR, "Could not allocate buffer\n");
                return 0;
            }
        }

        obuf[0] = s->buffer[0];

        if (av_audio_convert(s->convert_ctx[0], obuf, ostride,
                             ibuf, istride, nb_samples * s->input_channels) < 0) {
            av_log(s->resample_context, AV_LOG_ERROR,
                   "Audio sample format conversion failed\n");
            return 0;
        }

        input = s->buffer[0];
    }

    lenout= 2*s->output_channels*nb_samples * s->ratio + 16;

    if (s->sample_fmt[1] != AV_SAMPLE_FMT_S16) {
        output_bak = output;

        if (!s->buffer_size[1] || s->buffer_size[1] < 2*lenout) {
            av_free(s->buffer[1]);
            s->buffer_size[1] = 2*lenout;
            s->buffer[1] = av_malloc(s->buffer_size[1]);
            if (!s->buffer[1]) {
                av_log(s->resample_context, AV_LOG_ERROR, "Could not allocate buffer\n");
                return 0;
            }
        }

        output = s->buffer[1];
    }

    /* XXX: move those malloc to resample init code */
    for (i = 0; i < s->filter_channels; i++) {
        bufin[i] = av_malloc((nb_samples + s->temp_len) * sizeof(short));
        memcpy(bufin[i], s->temp[i], s->temp_len * sizeof(short));
        buftmp2[i] = bufin[i] + s->temp_len;
        bufout[i] = av_malloc(lenout * sizeof(short));
    }

    if (s->input_channels == 2 && s->output_channels == 1) {
        buftmp3[0] = output;
        stereo_to_mono(buftmp2[0], input, nb_samples);
    } else if (s->output_channels >= 2 && s->input_channels == 1) {
        buftmp3[0] = bufout[0];
        memcpy(buftmp2[0], input, nb_samples * sizeof(short));
    } else if (s->input_channels == 6 && s->output_channels ==2) {
        buftmp3[0] = bufout[0];
        buftmp3[1] = bufout[1];
        surround_to_stereo(buftmp2, input, s->input_channels, nb_samples);
    } else if (s->output_channels >= s->input_channels && s->input_channels >= 2) {
        for (i = 0; i < s->input_channels; i++) {
            buftmp3[i] = bufout[i];
        }
        deinterleave(buftmp2, input, s->input_channels, nb_samples);
    } else {
        buftmp3[0] = output;
        memcpy(buftmp2[0], input, nb_samples * sizeof(short));
    }

    nb_samples += s->temp_len;

    /* resample each channel */
    nb_samples1 = 0; /* avoid warning */
    for (i = 0; i < s->filter_channels; i++) {
        int consumed;
        int is_last = i + 1 == s->filter_channels;

        nb_samples1 = av_resample(s->resample_context, buftmp3[i], bufin[i],
                                  &consumed, nb_samples, lenout, is_last);
        s->temp_len = nb_samples - consumed;
        s->temp[i] = av_realloc(s->temp[i], s->temp_len * sizeof(short));
        memcpy(s->temp[i], bufin[i] + consumed, s->temp_len * sizeof(short));
    }

    if (s->output_channels == 2 && s->input_channels == 1) {
        mono_to_stereo(output, buftmp3[0], nb_samples1);
    } else if (s->output_channels == 6 && s->input_channels == 2) {
        ac3_5p1_mux(output, buftmp3[0], buftmp3[1], nb_samples1);
    } else if ((s->output_channels == s->input_channels && s->input_channels >= 2) ||
               (s->output_channels == 2 && s->input_channels == 6)) {
        interleave(output, buftmp3, s->output_channels, nb_samples1);
    }

    if (s->sample_fmt[1] != AV_SAMPLE_FMT_S16) {
        int istride[1] = { 2 };
        int ostride[1] = { s->sample_size[1] };
        const void *ibuf[1] = { output };
        void       *obuf[1] = { output_bak };

        if (av_audio_convert(s->convert_ctx[1], obuf, ostride,
                             ibuf, istride, nb_samples1 * s->output_channels) < 0) {
            av_log(s->resample_context, AV_LOG_ERROR,
                   "Audio sample format convertion failed\n");
            return 0;
        }
    }

    for (i = 0; i < s->filter_channels; i++) {
        av_free(bufin[i]);
        av_free(bufout[i]);
    }

    return nb_samples1;
}

void audio_resample_close(ReSampleContext *s)
{
    int i;
    av_resample_close(s->resample_context);
    for (i = 0; i < s->filter_channels; i++)
        av_freep(&s->temp[i]);
    av_freep(&s->buffer[0]);
    av_freep(&s->buffer[1]);
    av_audio_convert_free(s->convert_ctx[0]);
    av_audio_convert_free(s->convert_ctx[1]);
    av_free(s);
}
