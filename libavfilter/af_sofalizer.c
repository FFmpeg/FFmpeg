/*****************************************************************************
 * sofalizer.c : SOFAlizer filter for virtual binaural acoustics
 *****************************************************************************
 * Copyright (C) 2013-2015 Andreas Fuchs, Wolfgang Hrauda,
 *                         Acoustics Research Institute (ARI), Vienna, Austria
 *
 * Authors: Andreas Fuchs <andi.fuchs.mail@gmail.com>
 *          Wolfgang Hrauda <wolfgang.hrauda@gmx.at>
 *
 * SOFAlizer project coordinator at ARI, main developer of SOFA:
 *          Piotr Majdak <piotr@majdak.at>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#include <math.h>
#include <mysofa.h>

#include "libavcodec/avfft.h"
#include "libavutil/avstring.h"
#include "libavutil/channel_layout.h"
#include "libavutil/float_dsp.h"
#include "libavutil/intmath.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "internal.h"
#include "audio.h"

#define TIME_DOMAIN      0
#define FREQUENCY_DOMAIN 1

typedef struct MySofa {  /* contains data of one SOFA file */
    struct MYSOFA_EASY *easy;
    int n_samples;       /* length of one impulse response (IR) */
    float *lir, *rir;    /* IRs (time-domain) */
    int max_delay;
} MySofa;

typedef struct VirtualSpeaker {
    uint8_t set;
    float azim;
    float elev;
} VirtualSpeaker;

typedef struct SOFAlizerContext {
    const AVClass *class;

    char *filename;             /* name of SOFA file */
    MySofa sofa;                /* contains data of the SOFA file */

    int sample_rate;            /* sample rate from SOFA file */
    float *speaker_azim;        /* azimuth of the virtual loudspeakers */
    float *speaker_elev;        /* elevation of the virtual loudspeakers */
    char *speakers_pos;         /* custom positions of the virtual loudspeakers */
    float lfe_gain;             /* initial gain for the LFE channel */
    float gain_lfe;             /* gain applied to LFE channel */
    int lfe_channel;            /* LFE channel position in channel layout */

    int n_conv;                 /* number of channels to convolute */

                                /* buffer variables (for convolution) */
    float *ringbuffer[2];       /* buffers input samples, length of one buffer: */
                                /* no. input ch. (incl. LFE) x buffer_length */
    int write[2];               /* current write position to ringbuffer */
    int buffer_length;          /* is: longest IR plus max. delay in all SOFA files */
                                /* then choose next power of 2 */
    int n_fft;                  /* number of samples in one FFT block */

                                /* netCDF variables */
    int *delay[2];              /* broadband delay for each channel/IR to be convolved */

    float *data_ir[2];          /* IRs for all channels to be convolved */
                                /* (this excludes the LFE) */
    float *temp_src[2];
    FFTComplex *temp_fft[2];

                         /* control variables */
    float gain;          /* filter gain (in dB) */
    float rotation;      /* rotation of virtual loudspeakers (in degrees)  */
    float elevation;     /* elevation of virtual loudspeakers (in deg.) */
    float radius;        /* distance virtual loudspeakers to listener (in metres) */
    int type;            /* processing type */

    VirtualSpeaker vspkrpos[64];

    FFTContext *fft[2], *ifft[2];
    FFTComplex *data_hrtf[2];

    AVFloatDSPContext *fdsp;
} SOFAlizerContext;

static int close_sofa(struct MySofa *sofa)
{
    mysofa_close(sofa->easy);
    sofa->easy = NULL;

    return 0;
}

static int preload_sofa(AVFilterContext *ctx, char *filename, int *samplingrate)
{
    struct SOFAlizerContext *s = ctx->priv;
    struct MYSOFA_HRTF *mysofa;
    int ret;

    mysofa = mysofa_load(filename, &ret);
    if (ret || !mysofa) {
        av_log(ctx, AV_LOG_ERROR, "Can't find SOFA-file '%s'\n", filename);
        return AVERROR(EINVAL);
    }

    if (mysofa->DataSamplingRate.elements != 1)
        return AVERROR(EINVAL);
    *samplingrate = mysofa->DataSamplingRate.values[0];
    s->sofa.n_samples = mysofa->N;
    mysofa_free(mysofa);

    return 0;
}

static int parse_channel_name(char **arg, int *rchannel, char *buf)
{
    int len, i, channel_id = 0;
    int64_t layout, layout0;

    /* try to parse a channel name, e.g. "FL" */
    if (sscanf(*arg, "%7[A-Z]%n", buf, &len)) {
        layout0 = layout = av_get_channel_layout(buf);
        /* channel_id <- first set bit in layout */
        for (i = 32; i > 0; i >>= 1) {
            if (layout >= 1LL << i) {
                channel_id += i;
                layout >>= i;
            }
        }
        /* reject layouts that are not a single channel */
        if (channel_id >= 64 || layout0 != 1LL << channel_id)
            return AVERROR(EINVAL);
        *rchannel = channel_id;
        *arg += len;
        return 0;
    }
    return AVERROR(EINVAL);
}

static void parse_speaker_pos(AVFilterContext *ctx, int64_t in_channel_layout)
{
    SOFAlizerContext *s = ctx->priv;
    char *arg, *tokenizer, *p, *args = av_strdup(s->speakers_pos);

    if (!args)
        return;
    p = args;

    while ((arg = av_strtok(p, "|", &tokenizer))) {
        char buf[8];
        float azim, elev;
        int out_ch_id;

        p = NULL;
        if (parse_channel_name(&arg, &out_ch_id, buf)) {
            av_log(ctx, AV_LOG_WARNING, "Failed to parse \'%s\' as channel name.\n", buf);
            continue;
        }
        if (sscanf(arg, "%f %f", &azim, &elev) == 2) {
            s->vspkrpos[out_ch_id].set = 1;
            s->vspkrpos[out_ch_id].azim = azim;
            s->vspkrpos[out_ch_id].elev = elev;
        } else if (sscanf(arg, "%f", &azim) == 1) {
            s->vspkrpos[out_ch_id].set = 1;
            s->vspkrpos[out_ch_id].azim = azim;
            s->vspkrpos[out_ch_id].elev = 0;
        }
    }

    av_free(args);
}

static int get_speaker_pos(AVFilterContext *ctx,
                           float *speaker_azim, float *speaker_elev)
{
    struct SOFAlizerContext *s = ctx->priv;
    uint64_t channels_layout = ctx->inputs[0]->channel_layout;
    float azim[16] = { 0 };
    float elev[16] = { 0 };
    int m, ch, n_conv = ctx->inputs[0]->channels; /* get no. input channels */

    if (n_conv > 16)
        return AVERROR(EINVAL);

    s->lfe_channel = -1;

    if (s->speakers_pos)
        parse_speaker_pos(ctx, channels_layout);

    /* set speaker positions according to input channel configuration: */
    for (m = 0, ch = 0; ch < n_conv && m < 64; m++) {
        uint64_t mask = channels_layout & (1ULL << m);

        switch (mask) {
        case AV_CH_FRONT_LEFT:            azim[ch] =  30;      break;
        case AV_CH_FRONT_RIGHT:           azim[ch] = 330;      break;
        case AV_CH_FRONT_CENTER:          azim[ch] =   0;      break;
        case AV_CH_LOW_FREQUENCY:
        case AV_CH_LOW_FREQUENCY_2:       s->lfe_channel = ch; break;
        case AV_CH_BACK_LEFT:             azim[ch] = 150;      break;
        case AV_CH_BACK_RIGHT:            azim[ch] = 210;      break;
        case AV_CH_BACK_CENTER:           azim[ch] = 180;      break;
        case AV_CH_SIDE_LEFT:             azim[ch] =  90;      break;
        case AV_CH_SIDE_RIGHT:            azim[ch] = 270;      break;
        case AV_CH_FRONT_LEFT_OF_CENTER:  azim[ch] =  15;      break;
        case AV_CH_FRONT_RIGHT_OF_CENTER: azim[ch] = 345;      break;
        case AV_CH_TOP_CENTER:            azim[ch] =   0;
                                          elev[ch] =  90;      break;
        case AV_CH_TOP_FRONT_LEFT:        azim[ch] =  30;
                                          elev[ch] =  45;      break;
        case AV_CH_TOP_FRONT_CENTER:      azim[ch] =   0;
                                          elev[ch] =  45;      break;
        case AV_CH_TOP_FRONT_RIGHT:       azim[ch] = 330;
                                          elev[ch] =  45;      break;
        case AV_CH_TOP_BACK_LEFT:         azim[ch] = 150;
                                          elev[ch] =  45;      break;
        case AV_CH_TOP_BACK_RIGHT:        azim[ch] = 210;
                                          elev[ch] =  45;      break;
        case AV_CH_TOP_BACK_CENTER:       azim[ch] = 180;
                                          elev[ch] =  45;      break;
        case AV_CH_WIDE_LEFT:             azim[ch] =  90;      break;
        case AV_CH_WIDE_RIGHT:            azim[ch] = 270;      break;
        case AV_CH_SURROUND_DIRECT_LEFT:  azim[ch] =  90;      break;
        case AV_CH_SURROUND_DIRECT_RIGHT: azim[ch] = 270;      break;
        case AV_CH_STEREO_LEFT:           azim[ch] =  90;      break;
        case AV_CH_STEREO_RIGHT:          azim[ch] = 270;      break;
        case 0:                                                break;
        default:
            return AVERROR(EINVAL);
        }

        if (s->vspkrpos[m].set) {
            azim[ch] = s->vspkrpos[m].azim;
            elev[ch] = s->vspkrpos[m].elev;
        }

        if (mask)
            ch++;
    }

    memcpy(speaker_azim, azim, n_conv * sizeof(float));
    memcpy(speaker_elev, elev, n_conv * sizeof(float));

    return 0;

}

typedef struct ThreadData {
    AVFrame *in, *out;
    int *write;
    int **delay;
    float **ir;
    int *n_clippings;
    float **ringbuffer;
    float **temp_src;
    FFTComplex **temp_fft;
} ThreadData;

static int sofalizer_convolute(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    SOFAlizerContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *in = td->in, *out = td->out;
    int offset = jobnr;
    int *write = &td->write[jobnr];
    const int *const delay = td->delay[jobnr];
    const float *const ir = td->ir[jobnr];
    int *n_clippings = &td->n_clippings[jobnr];
    float *ringbuffer = td->ringbuffer[jobnr];
    float *temp_src = td->temp_src[jobnr];
    const int n_samples = s->sofa.n_samples; /* length of one IR */
    const float *src = (const float *)in->data[0]; /* get pointer to audio input buffer */
    float *dst = (float *)out->data[0]; /* get pointer to audio output buffer */
    const int in_channels = s->n_conv; /* number of input channels */
    /* ring buffer length is: longest IR plus max. delay -> next power of 2 */
    const int buffer_length = s->buffer_length;
    /* -1 for AND instead of MODULO (applied to powers of 2): */
    const uint32_t modulo = (uint32_t)buffer_length - 1;
    float *buffer[16]; /* holds ringbuffer for each input channel */
    int wr = *write;
    int read;
    int i, l;

    dst += offset;
    for (l = 0; l < in_channels; l++) {
        /* get starting address of ringbuffer for each input channel */
        buffer[l] = ringbuffer + l * buffer_length;
    }

    for (i = 0; i < in->nb_samples; i++) {
        const float *temp_ir = ir; /* using same set of IRs for each sample */

        dst[0] = 0;
        for (l = 0; l < in_channels; l++) {
            /* write current input sample to ringbuffer (for each channel) */
            buffer[l][wr] = src[l];
        }

        /* loop goes through all channels to be convolved */
        for (l = 0; l < in_channels; l++) {
            const float *const bptr = buffer[l];

            if (l == s->lfe_channel) {
                /* LFE is an input channel but requires no convolution */
                /* apply gain to LFE signal and add to output buffer */
                *dst += *(buffer[s->lfe_channel] + wr) * s->gain_lfe;
                temp_ir += FFALIGN(n_samples, 32);
                continue;
            }

            /* current read position in ringbuffer: input sample write position
             * - delay for l-th ch. + diff. betw. IR length and buffer length
             * (mod buffer length) */
            read = (wr - delay[l] - (n_samples - 1) + buffer_length) & modulo;

            if (read + n_samples < buffer_length) {
                memmove(temp_src, bptr + read, n_samples * sizeof(*temp_src));
            } else {
                int len = FFMIN(n_samples - (read % n_samples), buffer_length - read);

                memmove(temp_src, bptr + read, len * sizeof(*temp_src));
                memmove(temp_src + len, bptr, (n_samples - len) * sizeof(*temp_src));
            }

            /* multiply signal and IR, and add up the results */
            dst[0] += s->fdsp->scalarproduct_float(temp_ir, temp_src, n_samples);
            temp_ir += FFALIGN(n_samples, 32);
        }

        /* clippings counter */
        if (fabs(dst[0]) > 1)
            *n_clippings += 1;

        /* move output buffer pointer by +2 to get to next sample of processed channel: */
        dst += 2;
        src += in_channels;
        wr   = (wr + 1) & modulo; /* update ringbuffer write position */
    }

    *write = wr; /* remember write position in ringbuffer for next call */

    return 0;
}

static int sofalizer_fast_convolute(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    SOFAlizerContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *in = td->in, *out = td->out;
    int offset = jobnr;
    int *write = &td->write[jobnr];
    FFTComplex *hrtf = s->data_hrtf[jobnr]; /* get pointers to current HRTF data */
    int *n_clippings = &td->n_clippings[jobnr];
    float *ringbuffer = td->ringbuffer[jobnr];
    const int n_samples = s->sofa.n_samples; /* length of one IR */
    const float *src = (const float *)in->data[0]; /* get pointer to audio input buffer */
    float *dst = (float *)out->data[0]; /* get pointer to audio output buffer */
    const int in_channels = s->n_conv; /* number of input channels */
    /* ring buffer length is: longest IR plus max. delay -> next power of 2 */
    const int buffer_length = s->buffer_length;
    /* -1 for AND instead of MODULO (applied to powers of 2): */
    const uint32_t modulo = (uint32_t)buffer_length - 1;
    FFTComplex *fft_in = s->temp_fft[jobnr]; /* temporary array for FFT input/output data */
    FFTContext *ifft = s->ifft[jobnr];
    FFTContext *fft = s->fft[jobnr];
    const int n_conv = s->n_conv;
    const int n_fft = s->n_fft;
    const float fft_scale = 1.0f / s->n_fft;
    FFTComplex *hrtf_offset;
    int wr = *write;
    int n_read;
    int i, j;

    dst += offset;

    /* find minimum between number of samples and output buffer length:
     * (important, if one IR is longer than the output buffer) */
    n_read = FFMIN(s->sofa.n_samples, in->nb_samples);
    for (j = 0; j < n_read; j++) {
        /* initialize output buf with saved signal from overflow buf */
        dst[2 * j]     = ringbuffer[wr];
        ringbuffer[wr] = 0.0; /* re-set read samples to zero */
        /* update ringbuffer read/write position */
        wr  = (wr + 1) & modulo;
    }

    /* initialize rest of output buffer with 0 */
    for (j = n_read; j < in->nb_samples; j++) {
        dst[2 * j] = 0;
    }

    for (i = 0; i < n_conv; i++) {
        if (i == s->lfe_channel) { /* LFE */
            for (j = 0; j < in->nb_samples; j++) {
                /* apply gain to LFE signal and add to output buffer */
                dst[2 * j] += src[i + j * in_channels] * s->gain_lfe;
            }
            continue;
        }

        /* outer loop: go through all input channels to be convolved */
        offset = i * n_fft; /* no. samples already processed */
        hrtf_offset = hrtf + offset;

        /* fill FFT input with 0 (we want to zero-pad) */
        memset(fft_in, 0, sizeof(FFTComplex) * n_fft);

        for (j = 0; j < in->nb_samples; j++) {
            /* prepare input for FFT */
            /* write all samples of current input channel to FFT input array */
            fft_in[j].re = src[j * in_channels + i];
        }

        /* transform input signal of current channel to frequency domain */
        av_fft_permute(fft, fft_in);
        av_fft_calc(fft, fft_in);
        for (j = 0; j < n_fft; j++) {
            const FFTComplex *hcomplex = hrtf_offset + j;
            const float re = fft_in[j].re;
            const float im = fft_in[j].im;

            /* complex multiplication of input signal and HRTFs */
            /* output channel (real): */
            fft_in[j].re = re * hcomplex->re - im * hcomplex->im;
            /* output channel (imag): */
            fft_in[j].im = re * hcomplex->im + im * hcomplex->re;
        }

        /* transform output signal of current channel back to time domain */
        av_fft_permute(ifft, fft_in);
        av_fft_calc(ifft, fft_in);

        for (j = 0; j < in->nb_samples; j++) {
            /* write output signal of current channel to output buffer */
            dst[2 * j] += fft_in[j].re * fft_scale;
        }

        for (j = 0; j < n_samples - 1; j++) { /* overflow length is IR length - 1 */
            /* write the rest of output signal to overflow buffer */
            int write_pos = (wr + j) & modulo;

            *(ringbuffer + write_pos) += fft_in[in->nb_samples + j].re * fft_scale;
        }
    }

    /* go through all samples of current output buffer: count clippings */
    for (i = 0; i < out->nb_samples; i++) {
        /* clippings counter */
        if (fabs(*dst) > 1) { /* if current output sample > 1 */
            n_clippings[0]++;
        }

        /* move output buffer pointer by +2 to get to next sample of processed channel: */
        dst += 2;
    }

    /* remember read/write position in ringbuffer for next call */
    *write = wr;

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    SOFAlizerContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    int n_clippings[2] = { 0 };
    ThreadData td;
    AVFrame *out;

    out = ff_get_audio_buffer(outlink, in->nb_samples);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

    td.in = in; td.out = out; td.write = s->write;
    td.delay = s->delay; td.ir = s->data_ir; td.n_clippings = n_clippings;
    td.ringbuffer = s->ringbuffer; td.temp_src = s->temp_src;
    td.temp_fft = s->temp_fft;

    if (s->type == TIME_DOMAIN) {
        ctx->internal->execute(ctx, sofalizer_convolute, &td, NULL, 2);
    } else {
        ctx->internal->execute(ctx, sofalizer_fast_convolute, &td, NULL, 2);
    }
    emms_c();

    /* display error message if clipping occurred */
    if (n_clippings[0] + n_clippings[1] > 0) {
        av_log(ctx, AV_LOG_WARNING, "%d of %d samples clipped. Please reduce gain.\n",
               n_clippings[0] + n_clippings[1], out->nb_samples * 2);
    }

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static int query_formats(AVFilterContext *ctx)
{
    struct SOFAlizerContext *s = ctx->priv;
    AVFilterFormats *formats = NULL;
    AVFilterChannelLayouts *layouts = NULL;
    int ret, sample_rates[] = { 48000, -1 };

    ret = ff_add_format(&formats, AV_SAMPLE_FMT_FLT);
    if (ret)
        return ret;
    ret = ff_set_common_formats(ctx, formats);
    if (ret)
        return ret;

    layouts = ff_all_channel_layouts();
    if (!layouts)
        return AVERROR(ENOMEM);

    ret = ff_channel_layouts_ref(layouts, &ctx->inputs[0]->out_channel_layouts);
    if (ret)
        return ret;

    layouts = NULL;
    ret = ff_add_channel_layout(&layouts, AV_CH_LAYOUT_STEREO);
    if (ret)
        return ret;

    ret = ff_channel_layouts_ref(layouts, &ctx->outputs[0]->in_channel_layouts);
    if (ret)
        return ret;

    sample_rates[0] = s->sample_rate;
    formats = ff_make_format_list(sample_rates);
    if (!formats)
        return AVERROR(ENOMEM);
    return ff_set_common_samplerates(ctx, formats);
}

static int load_data(AVFilterContext *ctx, int azim, int elev, float radius, int sample_rate)
{
    struct SOFAlizerContext *s = ctx->priv;
    int n_samples;
    int n_conv = s->n_conv; /* no. channels to convolve */
    int n_fft;
    float delay_l; /* broadband delay for each IR */
    float delay_r;
    int nb_input_channels = ctx->inputs[0]->channels; /* no. input channels */
    float gain_lin = expf((s->gain - 3 * nb_input_channels) / 20 * M_LN10); /* gain - 3dB/channel */
    FFTComplex *data_hrtf_l = NULL;
    FFTComplex *data_hrtf_r = NULL;
    FFTComplex *fft_in_l = NULL;
    FFTComplex *fft_in_r = NULL;
    float *data_ir_l = NULL;
    float *data_ir_r = NULL;
    int offset = 0; /* used for faster pointer arithmetics in for-loop */
    int i, j, azim_orig = azim, elev_orig = elev;
    int filter_length, ret = 0;
    int n_current;
    int n_max = 0;

    s->sofa.easy = mysofa_open(s->filename, sample_rate, &filter_length, &ret);
    if (!s->sofa.easy || ret) { /* if an invalid SOFA file has been selected */
        av_log(ctx, AV_LOG_ERROR, "Selected SOFA file is invalid. Please select valid SOFA file.\n");
        return AVERROR_INVALIDDATA;
    }

    n_samples = s->sofa.n_samples;

    s->data_ir[0] = av_calloc(FFALIGN(n_samples, 32), sizeof(float) * s->n_conv);
    s->data_ir[1] = av_calloc(FFALIGN(n_samples, 32), sizeof(float) * s->n_conv);
    s->delay[0] = av_calloc(s->n_conv, sizeof(int));
    s->delay[1] = av_calloc(s->n_conv, sizeof(int));

    if (!s->data_ir[0] || !s->data_ir[1] || !s->delay[0] || !s->delay[1]) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    /* get temporary IR for L and R channel */
    data_ir_l = av_calloc(n_conv * FFALIGN(n_samples, 32), sizeof(*data_ir_l));
    data_ir_r = av_calloc(n_conv * FFALIGN(n_samples, 32), sizeof(*data_ir_r));
    if (!data_ir_r || !data_ir_l) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    if (s->type == TIME_DOMAIN) {
        s->temp_src[0] = av_calloc(FFALIGN(n_samples, 32), sizeof(float));
        s->temp_src[1] = av_calloc(FFALIGN(n_samples, 32), sizeof(float));
        if (!s->temp_src[0] || !s->temp_src[1]) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    }

    s->speaker_azim = av_calloc(s->n_conv, sizeof(*s->speaker_azim));
    s->speaker_elev = av_calloc(s->n_conv, sizeof(*s->speaker_elev));
    if (!s->speaker_azim || !s->speaker_elev) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    /* get speaker positions */
    if ((ret = get_speaker_pos(ctx, s->speaker_azim, s->speaker_elev)) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Couldn't get speaker positions. Input channel configuration not supported.\n");
        goto fail;
    }

    for (i = 0; i < s->n_conv; i++) {
        float coordinates[3];

        /* load and store IRs and corresponding delays */
        azim = (int)(s->speaker_azim[i] + azim_orig) % 360;
        elev = (int)(s->speaker_elev[i] + elev_orig) % 90;

        coordinates[0] = azim;
        coordinates[1] = elev;
        coordinates[2] = radius;

        mysofa_s2c(coordinates);

        /* get id of IR closest to desired position */
        mysofa_getfilter_float(s->sofa.easy, coordinates[0], coordinates[1], coordinates[2],
                               data_ir_l + FFALIGN(n_samples, 32) * i,
                               data_ir_r + FFALIGN(n_samples, 32) * i,
                               &delay_l, &delay_r);

        s->delay[0][i] = delay_l * sample_rate;
        s->delay[1][i] = delay_r * sample_rate;

        s->sofa.max_delay = FFMAX3(s->sofa.max_delay, s->delay[0][i], s->delay[1][i]);
    }

    /* get size of ringbuffer (longest IR plus max. delay) */
    /* then choose next power of 2 for performance optimization */
    n_current = s->sofa.n_samples + s->sofa.max_delay;
    /* length of longest IR plus max. delay */
    n_max = FFMAX(n_max, n_current);

    /* buffer length is longest IR plus max. delay -> next power of 2
       (32 - count leading zeros gives required exponent)  */
    s->buffer_length = 1 << (32 - ff_clz(n_max));
    s->n_fft = n_fft = 1 << (32 - ff_clz(n_max + sample_rate));

    if (s->type == FREQUENCY_DOMAIN) {
        av_fft_end(s->fft[0]);
        av_fft_end(s->fft[1]);
        s->fft[0] = av_fft_init(log2(s->n_fft), 0);
        s->fft[1] = av_fft_init(log2(s->n_fft), 0);
        av_fft_end(s->ifft[0]);
        av_fft_end(s->ifft[1]);
        s->ifft[0] = av_fft_init(log2(s->n_fft), 1);
        s->ifft[1] = av_fft_init(log2(s->n_fft), 1);

        if (!s->fft[0] || !s->fft[1] || !s->ifft[0] || !s->ifft[1]) {
            av_log(ctx, AV_LOG_ERROR, "Unable to create FFT contexts of size %d.\n", s->n_fft);
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    }

    if (s->type == TIME_DOMAIN) {
        s->ringbuffer[0] = av_calloc(s->buffer_length, sizeof(float) * nb_input_channels);
        s->ringbuffer[1] = av_calloc(s->buffer_length, sizeof(float) * nb_input_channels);
    } else {
        /* get temporary HRTF memory for L and R channel */
        data_hrtf_l = av_malloc_array(n_fft, sizeof(*data_hrtf_l) * n_conv);
        data_hrtf_r = av_malloc_array(n_fft, sizeof(*data_hrtf_r) * n_conv);
        if (!data_hrtf_r || !data_hrtf_l) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        s->ringbuffer[0] = av_calloc(s->buffer_length, sizeof(float));
        s->ringbuffer[1] = av_calloc(s->buffer_length, sizeof(float));
        s->temp_fft[0] = av_malloc_array(s->n_fft, sizeof(FFTComplex));
        s->temp_fft[1] = av_malloc_array(s->n_fft, sizeof(FFTComplex));
        if (!s->temp_fft[0] || !s->temp_fft[1]) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    }

    if (!s->ringbuffer[0] || !s->ringbuffer[1]) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    if (s->type == FREQUENCY_DOMAIN) {
        fft_in_l = av_calloc(n_fft, sizeof(*fft_in_l));
        fft_in_r = av_calloc(n_fft, sizeof(*fft_in_r));
        if (!fft_in_l || !fft_in_r) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    }

    for (i = 0; i < s->n_conv; i++) {
        float *lir, *rir;

        offset = i * FFALIGN(n_samples, 32); /* no. samples already written */

        lir = data_ir_l + offset;
        rir = data_ir_r + offset;

        if (s->type == TIME_DOMAIN) {
            for (j = 0; j < n_samples; j++) {
                /* load reversed IRs of the specified source position
                 * sample-by-sample for left and right ear; and apply gain */
                s->data_ir[0][offset + j] = lir[n_samples - 1 - j] * gain_lin;
                s->data_ir[1][offset + j] = rir[n_samples - 1 - j] * gain_lin;
            }
        } else {
            memset(fft_in_l, 0, n_fft * sizeof(*fft_in_l));
            memset(fft_in_r, 0, n_fft * sizeof(*fft_in_r));

            offset = i * n_fft; /* no. samples already written */
            for (j = 0; j < n_samples; j++) {
                /* load non-reversed IRs of the specified source position
                 * sample-by-sample and apply gain,
                 * L channel is loaded to real part, R channel to imag part,
                 * IRs ared shifted by L and R delay */
                fft_in_l[s->delay[0][i] + j].re = lir[j] * gain_lin;
                fft_in_r[s->delay[1][i] + j].re = rir[j] * gain_lin;
            }

            /* actually transform to frequency domain (IRs -> HRTFs) */
            av_fft_permute(s->fft[0], fft_in_l);
            av_fft_calc(s->fft[0], fft_in_l);
            memcpy(data_hrtf_l + offset, fft_in_l, n_fft * sizeof(*fft_in_l));
            av_fft_permute(s->fft[0], fft_in_r);
            av_fft_calc(s->fft[0], fft_in_r);
            memcpy(data_hrtf_r + offset, fft_in_r, n_fft * sizeof(*fft_in_r));
        }
    }

    if (s->type == FREQUENCY_DOMAIN) {
        s->data_hrtf[0] = av_malloc_array(n_fft * s->n_conv, sizeof(FFTComplex));
        s->data_hrtf[1] = av_malloc_array(n_fft * s->n_conv, sizeof(FFTComplex));
        if (!s->data_hrtf[0] || !s->data_hrtf[1]) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        memcpy(s->data_hrtf[0], data_hrtf_l, /* copy HRTF data to */
            sizeof(FFTComplex) * n_conv * n_fft); /* filter struct */
        memcpy(s->data_hrtf[1], data_hrtf_r,
            sizeof(FFTComplex) * n_conv * n_fft);
    }

fail:
    av_freep(&data_hrtf_l); /* free temporary HRTF memory */
    av_freep(&data_hrtf_r);

    av_freep(&data_ir_l); /* free temprary IR memory */
    av_freep(&data_ir_r);

    av_freep(&fft_in_l); /* free temporary FFT memory */
    av_freep(&fft_in_r);

    return ret;
}

static av_cold int init(AVFilterContext *ctx)
{
    SOFAlizerContext *s = ctx->priv;
    int ret;

    if (!s->filename) {
        av_log(ctx, AV_LOG_ERROR, "Valid SOFA filename must be set.\n");
        return AVERROR(EINVAL);
    }

    /* preload SOFA file, */
    ret = preload_sofa(ctx, s->filename, &s->sample_rate);
    if (ret) {
        /* file loading error */
        av_log(ctx, AV_LOG_ERROR, "Error while loading SOFA file: '%s'\n", s->filename);
    } else { /* no file loading error, resampling not required */
        av_log(ctx, AV_LOG_DEBUG, "File '%s' loaded.\n", s->filename);
    }

    if (ret) {
        av_log(ctx, AV_LOG_ERROR, "No valid SOFA file could be loaded. Please specify valid SOFA file.\n");
        return ret;
    }

    s->fdsp = avpriv_float_dsp_alloc(0);
    if (!s->fdsp)
        return AVERROR(ENOMEM);

    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    SOFAlizerContext *s = ctx->priv;
    int ret;

    if (s->type == FREQUENCY_DOMAIN) {
        inlink->partial_buf_size =
        inlink->min_samples =
        inlink->max_samples = inlink->sample_rate;
    }

    /* gain -3 dB per channel, -6 dB to get LFE on a similar level */
    s->gain_lfe = expf((s->gain - 3 * inlink->channels - 6 + s->lfe_gain) / 20 * M_LN10);

    s->n_conv = inlink->channels;

    /* load IRs to data_ir[0] and data_ir[1] for required directions */
    if ((ret = load_data(ctx, s->rotation, s->elevation, s->radius, inlink->sample_rate)) < 0)
        return ret;

    av_log(ctx, AV_LOG_DEBUG, "Samplerate: %d Channels to convolute: %d, Length of ringbuffer: %d x %d\n",
        inlink->sample_rate, s->n_conv, inlink->channels, s->buffer_length);

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    SOFAlizerContext *s = ctx->priv;

    close_sofa(&s->sofa);
    av_fft_end(s->ifft[0]);
    av_fft_end(s->ifft[1]);
    av_fft_end(s->fft[0]);
    av_fft_end(s->fft[1]);
    av_freep(&s->delay[0]);
    av_freep(&s->delay[1]);
    av_freep(&s->data_ir[0]);
    av_freep(&s->data_ir[1]);
    av_freep(&s->ringbuffer[0]);
    av_freep(&s->ringbuffer[1]);
    av_freep(&s->speaker_azim);
    av_freep(&s->speaker_elev);
    av_freep(&s->temp_src[0]);
    av_freep(&s->temp_src[1]);
    av_freep(&s->temp_fft[0]);
    av_freep(&s->temp_fft[1]);
    av_freep(&s->data_hrtf[0]);
    av_freep(&s->data_hrtf[1]);
    av_freep(&s->fdsp);
}

#define OFFSET(x) offsetof(SOFAlizerContext, x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption sofalizer_options[] = {
    { "sofa",      "sofa filename",  OFFSET(filename),  AV_OPT_TYPE_STRING, {.str=NULL},            .flags = FLAGS },
    { "gain",      "set gain in dB", OFFSET(gain),      AV_OPT_TYPE_FLOAT,  {.dbl=0},     -20,  40, .flags = FLAGS },
    { "rotation",  "set rotation"  , OFFSET(rotation),  AV_OPT_TYPE_FLOAT,  {.dbl=0},    -360, 360, .flags = FLAGS },
    { "elevation", "set elevation",  OFFSET(elevation), AV_OPT_TYPE_FLOAT,  {.dbl=0},     -90,  90, .flags = FLAGS },
    { "radius",    "set radius",     OFFSET(radius),    AV_OPT_TYPE_FLOAT,  {.dbl=1},       0,   3, .flags = FLAGS },
    { "type",      "set processing", OFFSET(type),      AV_OPT_TYPE_INT,    {.i64=1},       0,   1, .flags = FLAGS, "type" },
    { "time",      "time domain",      0,               AV_OPT_TYPE_CONST,  {.i64=0},       0,   0, .flags = FLAGS, "type" },
    { "freq",      "frequency domain", 0,               AV_OPT_TYPE_CONST,  {.i64=1},       0,   0, .flags = FLAGS, "type" },
    { "speakers",  "set speaker custom positions", OFFSET(speakers_pos), AV_OPT_TYPE_STRING,  {.str=0},    0, 0, .flags = FLAGS },
    { "lfegain",   "set lfe gain",                 OFFSET(lfe_gain),     AV_OPT_TYPE_FLOAT,   {.dbl=0},   -9, 9, .flags = FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(sofalizer);

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_input,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_AUDIO,
    },
    { NULL }
};

AVFilter ff_af_sofalizer = {
    .name          = "sofalizer",
    .description   = NULL_IF_CONFIG_SMALL("SOFAlizer (Spatially Oriented Format for Acoustics)."),
    .priv_size     = sizeof(SOFAlizerContext),
    .priv_class    = &sofalizer_class,
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = inputs,
    .outputs       = outputs,
    .flags         = AVFILTER_FLAG_SLICE_THREADS,
};
