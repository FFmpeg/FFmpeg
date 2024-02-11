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

#include "libavutil/tx.h"
#include "libavutil/avstring.h"
#include "libavutil/channel_layout.h"
#include "libavutil/float_dsp.h"
#include "libavutil/intmath.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "filters.h"
#include "formats.h"
#include "internal.h"
#include "audio.h"

#define TIME_DOMAIN      0
#define FREQUENCY_DOMAIN 1

typedef struct MySofa {  /* contains data of one SOFA file */
    struct MYSOFA_HRTF *hrtf;
    struct MYSOFA_LOOKUP *lookup;
    struct MYSOFA_NEIGHBORHOOD *neighborhood;
    int ir_samples;      /* length of one impulse response (IR) */
    int n_samples;       /* ir_samples to next power of 2 */
    float *lir, *rir;    /* IRs (time-domain) */
    float *fir;
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
    int nb_samples;

                                /* netCDF variables */
    int *delay[2];              /* broadband delay for each channel/IR to be convolved */

    float *data_ir[2];          /* IRs for all channels to be convolved */
                                /* (this excludes the LFE) */
    float *temp_src[2];
    AVComplexFloat *in_fft[2];   /* Array to hold input FFT values */
    AVComplexFloat *out_fft[2];  /* Array to hold output FFT values */
    AVComplexFloat *temp_afft[2];   /* Array to accumulate FFT values prior to IFFT */

                         /* control variables */
    float gain;          /* filter gain (in dB) */
    float rotation;      /* rotation of virtual loudspeakers (in degrees)  */
    float elevation;     /* elevation of virtual loudspeakers (in deg.) */
    float radius;        /* distance virtual loudspeakers to listener (in metres) */
    int type;            /* processing type */
    int framesize;       /* size of buffer */
    int normalize;       /* should all IRs be normalized upon import ? */
    int interpolate;     /* should wanted IRs be interpolated from neighbors ? */
    int minphase;        /* should all IRs be minphased upon import ? */
    float anglestep;     /* neighbor search angle step, in agles */
    float radstep;       /* neighbor search radius step, in meters */

    VirtualSpeaker vspkrpos[64];

    AVTXContext *fft[2], *ifft[2];
    av_tx_fn tx_fn[2], itx_fn[2];
    AVComplexFloat *data_hrtf[2];

    AVFloatDSPContext *fdsp;
} SOFAlizerContext;

static int close_sofa(struct MySofa *sofa)
{
    if (sofa->neighborhood)
        mysofa_neighborhood_free(sofa->neighborhood);
    sofa->neighborhood = NULL;
    if (sofa->lookup)
        mysofa_lookup_free(sofa->lookup);
    sofa->lookup = NULL;
    if (sofa->hrtf)
        mysofa_free(sofa->hrtf);
    sofa->hrtf = NULL;
    av_freep(&sofa->fir);

    return 0;
}

static int preload_sofa(AVFilterContext *ctx, char *filename, int *samplingrate)
{
    struct SOFAlizerContext *s = ctx->priv;
    struct MYSOFA_HRTF *mysofa;
    char *license;
    int ret;

    mysofa = mysofa_load(filename, &ret);
    s->sofa.hrtf = mysofa;
    if (ret || !mysofa) {
        av_log(ctx, AV_LOG_ERROR, "Can't find SOFA-file '%s'\n", filename);
        return AVERROR(EINVAL);
    }

    ret = mysofa_check(mysofa);
    if (ret != MYSOFA_OK) {
        av_log(ctx, AV_LOG_ERROR, "Selected SOFA file is invalid. Please select valid SOFA file.\n");
        return ret;
    }

    if (s->normalize)
        mysofa_loudness(s->sofa.hrtf);

    if (s->minphase)
        mysofa_minphase(s->sofa.hrtf, 0.01f);

    mysofa_tocartesian(s->sofa.hrtf);

    s->sofa.lookup = mysofa_lookup_init(s->sofa.hrtf);
    if (s->sofa.lookup == NULL)
        return AVERROR(EINVAL);

    if (s->interpolate)
        s->sofa.neighborhood = mysofa_neighborhood_init_withstepdefine(s->sofa.hrtf,
                                                                       s->sofa.lookup,
                                                                       s->anglestep,
                                                                       s->radstep);

    s->sofa.fir = av_calloc(s->sofa.hrtf->N * s->sofa.hrtf->R, sizeof(*s->sofa.fir));
    if (!s->sofa.fir)
        return AVERROR(ENOMEM);

    if (mysofa->DataSamplingRate.elements != 1)
        return AVERROR(EINVAL);
    av_log(ctx, AV_LOG_DEBUG, "Original IR length: %d.\n", mysofa->N);
    *samplingrate = mysofa->DataSamplingRate.values[0];
    license = mysofa_getAttribute(mysofa->attributes, (char *)"License");
    if (license)
        av_log(ctx, AV_LOG_INFO, "SOFA license: %s\n", license);

    return 0;
}

static int parse_channel_name(AVFilterContext *ctx, char **arg, int *rchannel)
{
    int len;
    enum AVChannel channel_id = 0;
    char buf[8] = {0};

    /* try to parse a channel name, e.g. "FL" */
    if (av_sscanf(*arg, "%7[A-Z]%n", buf, &len)) {
        channel_id = av_channel_from_string(buf);
        if (channel_id < 0 || channel_id >= 64) {
            av_log(ctx, AV_LOG_WARNING, "Failed to parse \'%s\' as channel name.\n", buf);
            return AVERROR(EINVAL);
        }

        *rchannel = channel_id;
        *arg += len;
        return 0;
    } else if (av_sscanf(*arg, "%d%n", &channel_id, &len) == 1) {
        if (channel_id < 0 || channel_id >= 64) {
            av_log(ctx, AV_LOG_WARNING, "Failed to parse \'%d\' as channel number.\n", channel_id);
            return AVERROR(EINVAL);
        }
        *rchannel = channel_id;
        *arg += len;
        return 0;
    }
    return AVERROR(EINVAL);
}

static void parse_speaker_pos(AVFilterContext *ctx)
{
    SOFAlizerContext *s = ctx->priv;
    char *arg, *tokenizer, *p, *args = av_strdup(s->speakers_pos);

    if (!args)
        return;
    p = args;

    while ((arg = av_strtok(p, "|", &tokenizer))) {
        float azim, elev;
        int out_ch_id;

        p = NULL;
        if (parse_channel_name(ctx, &arg, &out_ch_id)) {
            continue;
        }
        if (av_sscanf(arg, "%f %f", &azim, &elev) == 2) {
            s->vspkrpos[out_ch_id].set = 1;
            s->vspkrpos[out_ch_id].azim = azim;
            s->vspkrpos[out_ch_id].elev = elev;
        } else if (av_sscanf(arg, "%f", &azim) == 1) {
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
    AVChannelLayout *channel_layout = &ctx->inputs[0]->ch_layout;
    float azim[64] = { 0 };
    float elev[64] = { 0 };
    int ch, n_conv = ctx->inputs[0]->ch_layout.nb_channels; /* get no. input channels */

    if (n_conv < 0 || n_conv > 64)
        return AVERROR(EINVAL);

    s->lfe_channel = -1;

    if (s->speakers_pos)
        parse_speaker_pos(ctx);

    /* set speaker positions according to input channel configuration: */
    for (ch = 0; ch < n_conv; ch++) {
        int chan = av_channel_layout_channel_from_index(channel_layout, ch);

        switch (chan) {
        case AV_CHAN_FRONT_LEFT:          azim[ch] =  30;      break;
        case AV_CHAN_FRONT_RIGHT:         azim[ch] = 330;      break;
        case AV_CHAN_FRONT_CENTER:        azim[ch] =   0;      break;
        case AV_CHAN_LOW_FREQUENCY:
        case AV_CHAN_LOW_FREQUENCY_2:     s->lfe_channel = ch; break;
        case AV_CHAN_BACK_LEFT:           azim[ch] = 150;      break;
        case AV_CHAN_BACK_RIGHT:          azim[ch] = 210;      break;
        case AV_CHAN_BACK_CENTER:         azim[ch] = 180;      break;
        case AV_CHAN_SIDE_LEFT:           azim[ch] =  90;      break;
        case AV_CHAN_SIDE_RIGHT:          azim[ch] = 270;      break;
        case AV_CHAN_FRONT_LEFT_OF_CENTER:  azim[ch] =  15;    break;
        case AV_CHAN_FRONT_RIGHT_OF_CENTER: azim[ch] = 345;    break;
        case AV_CHAN_TOP_CENTER:          azim[ch] =   0;
                                          elev[ch] =  90;      break;
        case AV_CHAN_TOP_FRONT_LEFT:      azim[ch] =  30;
                                          elev[ch] =  45;      break;
        case AV_CHAN_TOP_FRONT_CENTER:    azim[ch] =   0;
                                          elev[ch] =  45;      break;
        case AV_CHAN_TOP_FRONT_RIGHT:     azim[ch] = 330;
                                          elev[ch] =  45;      break;
        case AV_CHAN_TOP_BACK_LEFT:       azim[ch] = 150;
                                          elev[ch] =  45;      break;
        case AV_CHAN_TOP_BACK_RIGHT:      azim[ch] = 210;
                                          elev[ch] =  45;      break;
        case AV_CHAN_TOP_BACK_CENTER:     azim[ch] = 180;
                                          elev[ch] =  45;      break;
        case AV_CHAN_WIDE_LEFT:           azim[ch] =  90;      break;
        case AV_CHAN_WIDE_RIGHT:          azim[ch] = 270;      break;
        case AV_CHAN_SURROUND_DIRECT_LEFT:  azim[ch] =  90;    break;
        case AV_CHAN_SURROUND_DIRECT_RIGHT: azim[ch] = 270;    break;
        case AV_CHAN_STEREO_LEFT:         azim[ch] =  90;      break;
        case AV_CHAN_STEREO_RIGHT:        azim[ch] = 270;      break;
        default:
            return AVERROR(EINVAL);
        }

        if (s->vspkrpos[ch].set) {
            azim[ch] = s->vspkrpos[ch].azim;
            elev[ch] = s->vspkrpos[ch].elev;
        }
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
    AVComplexFloat **in_fft;
    AVComplexFloat **out_fft;
    AVComplexFloat **temp_afft;
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
    const int ir_samples = s->sofa.ir_samples; /* length of one IR */
    const int n_samples = s->sofa.n_samples;
    const int planar = in->format == AV_SAMPLE_FMT_FLTP;
    const int mult = 1 + !planar;
    const float *src = (const float *)in->extended_data[0]; /* get pointer to audio input buffer */
    float *dst = (float *)out->extended_data[jobnr * planar]; /* get pointer to audio output buffer */
    const int in_channels = s->n_conv; /* number of input channels */
    /* ring buffer length is: longest IR plus max. delay -> next power of 2 */
    const int buffer_length = s->buffer_length;
    /* -1 for AND instead of MODULO (applied to powers of 2): */
    const uint32_t modulo = (uint32_t)buffer_length - 1;
    float *buffer[64]; /* holds ringbuffer for each input channel */
    int wr = *write;
    int read;
    int i, l;

    if (!planar)
        dst += offset;

    for (l = 0; l < in_channels; l++) {
        /* get starting address of ringbuffer for each input channel */
        buffer[l] = ringbuffer + l * buffer_length;
    }

    for (i = 0; i < in->nb_samples; i++) {
        const float *temp_ir = ir; /* using same set of IRs for each sample */

        dst[0] = 0;
        if (planar) {
            for (l = 0; l < in_channels; l++) {
                const float *srcp = (const float *)in->extended_data[l];

                /* write current input sample to ringbuffer (for each channel) */
                buffer[l][wr] = srcp[i];
            }
        } else {
            for (l = 0; l < in_channels; l++) {
                /* write current input sample to ringbuffer (for each channel) */
                buffer[l][wr] = src[l];
            }
        }

        /* loop goes through all channels to be convolved */
        for (l = 0; l < in_channels; l++) {
            const float *const bptr = buffer[l];

            if (l == s->lfe_channel) {
                /* LFE is an input channel but requires no convolution */
                /* apply gain to LFE signal and add to output buffer */
                dst[0] += *(buffer[s->lfe_channel] + wr) * s->gain_lfe;
                temp_ir += n_samples;
                continue;
            }

            /* current read position in ringbuffer: input sample write position
             * - delay for l-th ch. + diff. betw. IR length and buffer length
             * (mod buffer length) */
            read = (wr - delay[l] - (ir_samples - 1) + buffer_length) & modulo;

            if (read + ir_samples < buffer_length) {
                memmove(temp_src, bptr + read, ir_samples * sizeof(*temp_src));
            } else {
                int len = FFMIN(n_samples - (read % ir_samples), buffer_length - read);

                memmove(temp_src, bptr + read, len * sizeof(*temp_src));
                memmove(temp_src + len, bptr, (n_samples - len) * sizeof(*temp_src));
            }

            /* multiply signal and IR, and add up the results */
            dst[0] += s->fdsp->scalarproduct_float(temp_ir, temp_src, FFALIGN(ir_samples, 32));
            temp_ir += n_samples;
        }

        /* clippings counter */
        if (fabsf(dst[0]) > 1)
            n_clippings[0]++;

        /* move output buffer pointer by +2 to get to next sample of processed channel: */
        dst += mult;
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
    AVComplexFloat *hrtf = s->data_hrtf[jobnr]; /* get pointers to current HRTF data */
    int *n_clippings = &td->n_clippings[jobnr];
    float *ringbuffer = td->ringbuffer[jobnr];
    const int ir_samples = s->sofa.ir_samples; /* length of one IR */
    const int planar = in->format == AV_SAMPLE_FMT_FLTP;
    const int mult = 1 + !planar;
    float *dst = (float *)out->extended_data[jobnr * planar]; /* get pointer to audio output buffer */
    const int in_channels = s->n_conv; /* number of input channels */
    /* ring buffer length is: longest IR plus max. delay -> next power of 2 */
    const int buffer_length = s->buffer_length;
    /* -1 for AND instead of MODULO (applied to powers of 2): */
    const uint32_t modulo = (uint32_t)buffer_length - 1;
    AVComplexFloat *fft_in = s->in_fft[jobnr]; /* temporary array for FFT input data */
    AVComplexFloat *fft_out = s->out_fft[jobnr]; /* temporary array for FFT output data */
    AVComplexFloat *fft_acc = s->temp_afft[jobnr];
    AVTXContext *ifft = s->ifft[jobnr];
    av_tx_fn itx_fn = s->itx_fn[jobnr];
    AVTXContext *fft = s->fft[jobnr];
    av_tx_fn tx_fn = s->tx_fn[jobnr];
    const int n_conv = s->n_conv;
    const int n_fft = s->n_fft;
    const float fft_scale = 1.0f / s->n_fft;
    AVComplexFloat *hrtf_offset;
    int wr = *write;
    int n_read;
    int i, j;

    if (!planar)
        dst += offset;

    /* find minimum between number of samples and output buffer length:
     * (important, if one IR is longer than the output buffer) */
    n_read = FFMIN(ir_samples, in->nb_samples);
    for (j = 0; j < n_read; j++) {
        /* initialize output buf with saved signal from overflow buf */
        dst[mult * j]  = ringbuffer[wr];
        ringbuffer[wr] = 0.0f; /* re-set read samples to zero */
        /* update ringbuffer read/write position */
        wr  = (wr + 1) & modulo;
    }

    /* initialize rest of output buffer with 0 */
    for (j = n_read; j < in->nb_samples; j++) {
        dst[mult * j] = 0;
    }

    /* fill FFT accumulation with 0 */
    memset(fft_acc, 0, sizeof(AVComplexFloat) * n_fft);

    for (i = 0; i < n_conv; i++) {
        const float *src = (const float *)in->extended_data[i * planar]; /* get pointer to audio input buffer */

        if (i == s->lfe_channel) { /* LFE */
            if (in->format == AV_SAMPLE_FMT_FLT) {
                for (j = 0; j < in->nb_samples; j++) {
                    /* apply gain to LFE signal and add to output buffer */
                    dst[2 * j] += src[i + j * in_channels] * s->gain_lfe;
                }
            } else {
                for (j = 0; j < in->nb_samples; j++) {
                    /* apply gain to LFE signal and add to output buffer */
                    dst[j] += src[j] * s->gain_lfe;
                }
            }
            continue;
        }

        /* outer loop: go through all input channels to be convolved */
        offset = i * n_fft; /* no. samples already processed */
        hrtf_offset = hrtf + offset;

        /* fill FFT input with 0 (we want to zero-pad) */
        memset(fft_in, 0, sizeof(AVComplexFloat) * n_fft);

        if (in->format == AV_SAMPLE_FMT_FLT) {
            for (j = 0; j < in->nb_samples; j++) {
                /* prepare input for FFT */
                /* write all samples of current input channel to FFT input array */
                fft_in[j].re = src[j * in_channels + i];
            }
        } else {
            for (j = 0; j < in->nb_samples; j++) {
                /* prepare input for FFT */
                /* write all samples of current input channel to FFT input array */
                fft_in[j].re = src[j];
            }
        }

        /* transform input signal of current channel to frequency domain */
        tx_fn(fft, fft_out, fft_in, sizeof(*fft_in));

        for (j = 0; j < n_fft; j++) {
            const AVComplexFloat *hcomplex = hrtf_offset + j;
            const float re = fft_out[j].re;
            const float im = fft_out[j].im;

            /* complex multiplication of input signal and HRTFs */
            /* output channel (real): */
            fft_acc[j].re += re * hcomplex->re - im * hcomplex->im;
            /* output channel (imag): */
            fft_acc[j].im += re * hcomplex->im + im * hcomplex->re;
        }
    }

    /* transform output signal of current channel back to time domain */
    itx_fn(ifft, fft_out, fft_acc, sizeof(*fft_acc));

    for (j = 0; j < in->nb_samples; j++) {
        /* write output signal of current channel to output buffer */
        dst[mult * j] += fft_out[j].re * fft_scale;
    }

    for (j = 0; j < ir_samples - 1; j++) { /* overflow length is IR length - 1 */
        /* write the rest of output signal to overflow buffer */
        int write_pos = (wr + j) & modulo;

        *(ringbuffer + write_pos) += fft_out[in->nb_samples + j].re * fft_scale;
    }

    /* go through all samples of current output buffer: count clippings */
    for (i = 0; i < out->nb_samples; i++) {
        /* clippings counter */
        if (fabsf(dst[i * mult]) > 1) { /* if current output sample > 1 */
            n_clippings[0]++;
        }
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
    td.in_fft = s->in_fft;
    td.out_fft = s->out_fft;
    td.temp_afft = s->temp_afft;

    if (s->type == TIME_DOMAIN) {
        ff_filter_execute(ctx, sofalizer_convolute, &td, NULL, 2);
    } else if (s->type == FREQUENCY_DOMAIN) {
        ff_filter_execute(ctx, sofalizer_fast_convolute, &td, NULL, 2);
    }

    /* display error message if clipping occurred */
    if (n_clippings[0] + n_clippings[1] > 0) {
        av_log(ctx, AV_LOG_WARNING, "%d of %d samples clipped. Please reduce gain.\n",
               n_clippings[0] + n_clippings[1], out->nb_samples * 2);
    }

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static int activate(AVFilterContext *ctx)
{
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    SOFAlizerContext *s = ctx->priv;
    AVFrame *in;
    int ret;

    FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink);

    if (s->nb_samples)
        ret = ff_inlink_consume_samples(inlink, s->nb_samples, s->nb_samples, &in);
    else
        ret = ff_inlink_consume_frame(inlink, &in);
    if (ret < 0)
        return ret;
    if (ret > 0)
        return filter_frame(inlink, in);

    FF_FILTER_FORWARD_STATUS(inlink, outlink);
    FF_FILTER_FORWARD_WANTED(outlink, inlink);

    return FFERROR_NOT_READY;
}

static int query_formats(AVFilterContext *ctx)
{
    struct SOFAlizerContext *s = ctx->priv;
    AVFilterChannelLayouts *layouts = NULL;
    int ret, sample_rates[] = { 48000, -1 };
    static const enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_FLTP,
        AV_SAMPLE_FMT_NONE
    };

    ret = ff_set_common_formats_from_list(ctx, sample_fmts);
    if (ret)
        return ret;

    layouts = ff_all_channel_layouts();
    if (!layouts)
        return AVERROR(ENOMEM);

    ret = ff_channel_layouts_ref(layouts, &ctx->inputs[0]->outcfg.channel_layouts);
    if (ret)
        return ret;

    layouts = NULL;
    ret = ff_add_channel_layout(&layouts, &(AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO);
    if (ret)
        return ret;

    ret = ff_channel_layouts_ref(layouts, &ctx->outputs[0]->incfg.channel_layouts);
    if (ret)
        return ret;

    sample_rates[0] = s->sample_rate;
    return ff_set_common_samplerates_from_list(ctx, sample_rates);
}

static int getfilter_float(AVFilterContext *ctx, float x, float y, float z,
                           float *left, float *right,
                           float *delay_left, float *delay_right)
{
    struct SOFAlizerContext *s = ctx->priv;
    float c[3], delays[2];
    float *fl, *fr;
    int nearest;
    int *neighbors;
    float *res;

    c[0] = x, c[1] = y, c[2] = z;
    nearest = mysofa_lookup(s->sofa.lookup, c);
    if (nearest < 0)
        return AVERROR(EINVAL);

    if (s->interpolate) {
        neighbors = mysofa_neighborhood(s->sofa.neighborhood, nearest);
        res = mysofa_interpolate(s->sofa.hrtf, c,
                                 nearest, neighbors,
                                 s->sofa.fir, delays);
    } else {
        if (s->sofa.hrtf->DataDelay.elements > s->sofa.hrtf->R) {
            delays[0] = s->sofa.hrtf->DataDelay.values[nearest * s->sofa.hrtf->R];
            delays[1] = s->sofa.hrtf->DataDelay.values[nearest * s->sofa.hrtf->R + 1];
        } else {
            delays[0] = s->sofa.hrtf->DataDelay.values[0];
            delays[1] = s->sofa.hrtf->DataDelay.values[1];
        }
        res = s->sofa.hrtf->DataIR.values + nearest * s->sofa.hrtf->N * s->sofa.hrtf->R;
    }

    *delay_left  = delays[0];
    *delay_right = delays[1];

    fl = res;
    fr = res + s->sofa.hrtf->N;

    memcpy(left, fl, sizeof(float) * s->sofa.hrtf->N);
    memcpy(right, fr, sizeof(float) * s->sofa.hrtf->N);

    return 0;
}

static int load_data(AVFilterContext *ctx, int azim, int elev, float radius, int sample_rate)
{
    struct SOFAlizerContext *s = ctx->priv;
    int n_samples;
    int ir_samples;
    int n_conv = s->n_conv; /* no. channels to convolve */
    int n_fft;
    float delay_l; /* broadband delay for each IR */
    float delay_r;
    int nb_input_channels = ctx->inputs[0]->ch_layout.nb_channels; /* no. input channels */
    float gain_lin = expf((s->gain - 3 * nb_input_channels) / 20 * M_LN10); /* gain - 3dB/channel */
    AVComplexFloat *data_hrtf_l = NULL;
    AVComplexFloat *data_hrtf_r = NULL;
    AVComplexFloat *fft_out_l = NULL;
    AVComplexFloat *fft_out_r = NULL;
    AVComplexFloat *fft_in_l = NULL;
    AVComplexFloat *fft_in_r = NULL;
    float *data_ir_l = NULL;
    float *data_ir_r = NULL;
    int offset = 0; /* used for faster pointer arithmetics in for-loop */
    int i, j, azim_orig = azim, elev_orig = elev;
    int ret = 0;
    int n_current;
    int n_max = 0;

    av_log(ctx, AV_LOG_DEBUG, "IR length: %d.\n", s->sofa.hrtf->N);
    s->sofa.ir_samples = s->sofa.hrtf->N;
    s->sofa.n_samples = 1 << (32 - ff_clz(s->sofa.ir_samples));

    n_samples = s->sofa.n_samples;
    ir_samples = s->sofa.ir_samples;

    if (s->type == TIME_DOMAIN) {
        s->data_ir[0] = av_calloc(n_samples, sizeof(float) * s->n_conv);
        s->data_ir[1] = av_calloc(n_samples, sizeof(float) * s->n_conv);

        if (!s->data_ir[0] || !s->data_ir[1]) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    }

    s->delay[0] = av_calloc(s->n_conv, sizeof(int));
    s->delay[1] = av_calloc(s->n_conv, sizeof(int));

    if (!s->delay[0] || !s->delay[1]) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    /* get temporary IR for L and R channel */
    data_ir_l = av_calloc(n_conv * n_samples, sizeof(*data_ir_l));
    data_ir_r = av_calloc(n_conv * n_samples, sizeof(*data_ir_r));
    if (!data_ir_r || !data_ir_l) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    if (s->type == TIME_DOMAIN) {
        s->temp_src[0] = av_calloc(n_samples, sizeof(float));
        s->temp_src[1] = av_calloc(n_samples, sizeof(float));
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
        ret = getfilter_float(ctx, coordinates[0], coordinates[1], coordinates[2],
                              data_ir_l + n_samples * i,
                              data_ir_r + n_samples * i,
                              &delay_l, &delay_r);
        if (ret < 0)
            goto fail;

        s->delay[0][i] = delay_l * sample_rate;
        s->delay[1][i] = delay_r * sample_rate;

        s->sofa.max_delay = FFMAX3(s->sofa.max_delay, s->delay[0][i], s->delay[1][i]);
    }

    /* get size of ringbuffer (longest IR plus max. delay) */
    /* then choose next power of 2 for performance optimization */
    n_current = n_samples + s->sofa.max_delay;
    /* length of longest IR plus max. delay */
    n_max = FFMAX(n_max, n_current);

    /* buffer length is longest IR plus max. delay -> next power of 2
       (32 - count leading zeros gives required exponent)  */
    s->buffer_length = 1 << (32 - ff_clz(n_max));
    s->n_fft = n_fft = 1 << (32 - ff_clz(n_max + s->framesize));

    if (s->type == FREQUENCY_DOMAIN) {
        float scale = 1.f;

        av_tx_uninit(&s->fft[0]);
        av_tx_uninit(&s->fft[1]);
        ret = av_tx_init(&s->fft[0], &s->tx_fn[0], AV_TX_FLOAT_FFT, 0, s->n_fft, &scale, 0);
        if (ret < 0)
            goto fail;
        ret = av_tx_init(&s->fft[1], &s->tx_fn[1], AV_TX_FLOAT_FFT, 0, s->n_fft, &scale, 0);
        if (ret < 0)
            goto fail;
        av_tx_uninit(&s->ifft[0]);
        av_tx_uninit(&s->ifft[1]);
        ret = av_tx_init(&s->ifft[0], &s->itx_fn[0], AV_TX_FLOAT_FFT, 1, s->n_fft, &scale, 0);
        if (ret < 0)
            goto fail;
        ret = av_tx_init(&s->ifft[1], &s->itx_fn[1], AV_TX_FLOAT_FFT, 1, s->n_fft, &scale, 0);
        if (ret < 0)
            goto fail;
    }

    if (s->type == TIME_DOMAIN) {
        s->ringbuffer[0] = av_calloc(s->buffer_length, sizeof(float) * nb_input_channels);
        s->ringbuffer[1] = av_calloc(s->buffer_length, sizeof(float) * nb_input_channels);
    } else if (s->type == FREQUENCY_DOMAIN) {
        /* get temporary HRTF memory for L and R channel */
        data_hrtf_l = av_malloc_array(n_fft, sizeof(*data_hrtf_l) * n_conv);
        data_hrtf_r = av_malloc_array(n_fft, sizeof(*data_hrtf_r) * n_conv);
        if (!data_hrtf_r || !data_hrtf_l) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        s->ringbuffer[0] = av_calloc(s->buffer_length, sizeof(float));
        s->ringbuffer[1] = av_calloc(s->buffer_length, sizeof(float));
        s->in_fft[0] = av_malloc_array(s->n_fft, sizeof(AVComplexFloat));
        s->in_fft[1] = av_malloc_array(s->n_fft, sizeof(AVComplexFloat));
        s->out_fft[0] = av_malloc_array(s->n_fft, sizeof(AVComplexFloat));
        s->out_fft[1] = av_malloc_array(s->n_fft, sizeof(AVComplexFloat));
        s->temp_afft[0] = av_malloc_array(s->n_fft, sizeof(AVComplexFloat));
        s->temp_afft[1] = av_malloc_array(s->n_fft, sizeof(AVComplexFloat));
        if (!s->in_fft[0] || !s->in_fft[1] ||
            !s->out_fft[0] || !s->out_fft[1] ||
            !s->temp_afft[0] || !s->temp_afft[1]) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    }

    if (!s->ringbuffer[0] || !s->ringbuffer[1]) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    if (s->type == FREQUENCY_DOMAIN) {
        fft_out_l = av_calloc(n_fft, sizeof(*fft_out_l));
        fft_out_r = av_calloc(n_fft, sizeof(*fft_out_r));
        fft_in_l = av_calloc(n_fft, sizeof(*fft_in_l));
        fft_in_r = av_calloc(n_fft, sizeof(*fft_in_r));
        if (!fft_in_l || !fft_in_r ||
            !fft_out_l || !fft_out_r) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    }

    for (i = 0; i < s->n_conv; i++) {
        float *lir, *rir;

        offset = i * n_samples; /* no. samples already written */

        lir = data_ir_l + offset;
        rir = data_ir_r + offset;

        if (s->type == TIME_DOMAIN) {
            for (j = 0; j < ir_samples; j++) {
                /* load reversed IRs of the specified source position
                 * sample-by-sample for left and right ear; and apply gain */
                s->data_ir[0][offset + j] = lir[ir_samples - 1 - j] * gain_lin;
                s->data_ir[1][offset + j] = rir[ir_samples - 1 - j] * gain_lin;
            }
        } else if (s->type == FREQUENCY_DOMAIN) {
            memset(fft_in_l, 0, n_fft * sizeof(*fft_in_l));
            memset(fft_in_r, 0, n_fft * sizeof(*fft_in_r));

            offset = i * n_fft; /* no. samples already written */
            for (j = 0; j < ir_samples; j++) {
                /* load non-reversed IRs of the specified source position
                 * sample-by-sample and apply gain,
                 * L channel is loaded to real part, R channel to imag part,
                 * IRs are shifted by L and R delay */
                fft_in_l[s->delay[0][i] + j].re = lir[j] * gain_lin;
                fft_in_r[s->delay[1][i] + j].re = rir[j] * gain_lin;
            }

            /* actually transform to frequency domain (IRs -> HRTFs) */
            s->tx_fn[0](s->fft[0], fft_out_l, fft_in_l, sizeof(*fft_in_l));
            memcpy(data_hrtf_l + offset, fft_out_l, n_fft * sizeof(*fft_out_l));
            s->tx_fn[1](s->fft[1], fft_out_r, fft_in_r, sizeof(*fft_in_r));
            memcpy(data_hrtf_r + offset, fft_out_r, n_fft * sizeof(*fft_out_r));
        }
    }

    if (s->type == FREQUENCY_DOMAIN) {
        s->data_hrtf[0] = av_malloc_array(n_fft * s->n_conv, sizeof(AVComplexFloat));
        s->data_hrtf[1] = av_malloc_array(n_fft * s->n_conv, sizeof(AVComplexFloat));
        if (!s->data_hrtf[0] || !s->data_hrtf[1]) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        memcpy(s->data_hrtf[0], data_hrtf_l, /* copy HRTF data to */
            sizeof(AVComplexFloat) * n_conv * n_fft); /* filter struct */
        memcpy(s->data_hrtf[1], data_hrtf_r,
            sizeof(AVComplexFloat) * n_conv * n_fft);
    }

fail:
    av_freep(&data_hrtf_l); /* free temporary HRTF memory */
    av_freep(&data_hrtf_r);

    av_freep(&data_ir_l); /* free temprary IR memory */
    av_freep(&data_ir_r);

    av_freep(&fft_out_l); /* free temporary FFT memory */
    av_freep(&fft_out_r);

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

    if (s->type == FREQUENCY_DOMAIN)
        s->nb_samples = s->framesize;

    /* gain -3 dB per channel */
    s->gain_lfe = expf((s->gain - 3 * inlink->ch_layout.nb_channels + s->lfe_gain) / 20 * M_LN10);

    s->n_conv = inlink->ch_layout.nb_channels;

    /* load IRs to data_ir[0] and data_ir[1] for required directions */
    if ((ret = load_data(ctx, s->rotation, s->elevation, s->radius, inlink->sample_rate)) < 0)
        return ret;

    av_log(ctx, AV_LOG_DEBUG, "Samplerate: %d Channels to convolute: %d, Length of ringbuffer: %d x %d\n",
        inlink->sample_rate, s->n_conv, inlink->ch_layout.nb_channels, s->buffer_length);

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    SOFAlizerContext *s = ctx->priv;

    close_sofa(&s->sofa);
    av_tx_uninit(&s->ifft[0]);
    av_tx_uninit(&s->ifft[1]);
    av_tx_uninit(&s->fft[0]);
    av_tx_uninit(&s->fft[1]);
    s->ifft[0] = NULL;
    s->ifft[1] = NULL;
    s->fft[0] = NULL;
    s->fft[1] = NULL;
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
    av_freep(&s->temp_afft[0]);
    av_freep(&s->temp_afft[1]);
    av_freep(&s->in_fft[0]);
    av_freep(&s->in_fft[1]);
    av_freep(&s->out_fft[0]);
    av_freep(&s->out_fft[1]);
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
    { "radius",    "set radius",     OFFSET(radius),    AV_OPT_TYPE_FLOAT,  {.dbl=1},       0,   5, .flags = FLAGS },
    { "type",      "set processing", OFFSET(type),      AV_OPT_TYPE_INT,    {.i64=1},       0,   1, .flags = FLAGS, .unit = "type" },
    { "time",      "time domain",      0,               AV_OPT_TYPE_CONST,  {.i64=0},       0,   0, .flags = FLAGS, .unit = "type" },
    { "freq",      "frequency domain", 0,               AV_OPT_TYPE_CONST,  {.i64=1},       0,   0, .flags = FLAGS, .unit = "type" },
    { "speakers",  "set speaker custom positions", OFFSET(speakers_pos), AV_OPT_TYPE_STRING,  {.str=0},    0, 0, .flags = FLAGS },
    { "lfegain",   "set lfe gain",                 OFFSET(lfe_gain),     AV_OPT_TYPE_FLOAT,   {.dbl=0},  -20,40, .flags = FLAGS },
    { "framesize", "set frame size", OFFSET(framesize), AV_OPT_TYPE_INT,    {.i64=1024},1024,96000, .flags = FLAGS },
    { "normalize", "normalize IRs",  OFFSET(normalize), AV_OPT_TYPE_BOOL,   {.i64=1},       0,   1, .flags = FLAGS },
    { "interpolate","interpolate IRs from neighbors",   OFFSET(interpolate),AV_OPT_TYPE_BOOL,    {.i64=0},       0,   1, .flags = FLAGS },
    { "minphase",  "minphase IRs",   OFFSET(minphase),  AV_OPT_TYPE_BOOL,   {.i64=0},       0,   1, .flags = FLAGS },
    { "anglestep", "set neighbor search angle step",    OFFSET(anglestep),  AV_OPT_TYPE_FLOAT,   {.dbl=.5},      0.01, 10, .flags = FLAGS },
    { "radstep",   "set neighbor search radius step",   OFFSET(radstep),    AV_OPT_TYPE_FLOAT,   {.dbl=.01},     0.01,  1, .flags = FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(sofalizer);

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_input,
    },
};

const AVFilter ff_af_sofalizer = {
    .name          = "sofalizer",
    .description   = NULL_IF_CONFIG_SMALL("SOFAlizer (Spatially Oriented Format for Acoustics)."),
    .priv_size     = sizeof(SOFAlizerContext),
    .priv_class    = &sofalizer_class,
    .init          = init,
    .activate      = activate,
    .uninit        = uninit,
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(ff_audio_default_filterpad),
    FILTER_QUERY_FUNC(query_formats),
    .flags         = AVFILTER_FLAG_SLICE_THREADS,
};
