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
#include <netcdf.h>

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

typedef struct NCSofa {  /* contains data of one SOFA file */
    int ncid;            /* netCDF ID of the opened SOFA file */
    int n_samples;       /* length of one impulse response (IR) */
    int m_dim;           /* number of measurement positions */
    int *data_delay;     /* broadband delay of each IR */
                         /* all measurement positions for each receiver (i.e. ear): */
    float *sp_a;         /* azimuth angles */
    float *sp_e;         /* elevation angles */
    float *sp_r;         /* radii */
                         /* data at each measurement position for each receiver: */
    float *data_ir;      /* IRs (time-domain) */
} NCSofa;

typedef struct VirtualSpeaker {
    uint8_t set;
    float azim;
    float elev;
} VirtualSpeaker;

typedef struct SOFAlizerContext {
    const AVClass *class;

    char *filename;             /* name of SOFA file */
    NCSofa sofa;                /* contains data of the SOFA file */

    int sample_rate;            /* sample rate from SOFA file */
    float *speaker_azim;        /* azimuth of the virtual loudspeakers */
    float *speaker_elev;        /* elevation of the virtual loudspeakers */
    char *speakers_pos;         /* custom positions of the virtual loudspeakers */
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

static int close_sofa(struct NCSofa *sofa)
{
    av_freep(&sofa->data_delay);
    av_freep(&sofa->sp_a);
    av_freep(&sofa->sp_e);
    av_freep(&sofa->sp_r);
    av_freep(&sofa->data_ir);
    nc_close(sofa->ncid);
    sofa->ncid = 0;

    return 0;
}

static int load_sofa(AVFilterContext *ctx, char *filename, int *samplingrate)
{
    struct SOFAlizerContext *s = ctx->priv;
    /* variables associated with content of SOFA file: */
    int ncid, n_dims, n_vars, n_gatts, n_unlim_dim_id, status;
    char data_delay_dim_name[NC_MAX_NAME];
    float *sp_a, *sp_e, *sp_r, *data_ir;
    char *sofa_conventions;
    char dim_name[NC_MAX_NAME];   /* names of netCDF dimensions */
    size_t *dim_length;           /* lengths of netCDF dimensions */
    char *text;
    unsigned int sample_rate;
    int data_delay_dim_id[2];
    int samplingrate_id;
    int data_delay_id;
    int n_samples;
    int m_dim_id = -1;
    int n_dim_id = -1;
    int data_ir_id;
    size_t att_len;
    int m_dim;
    int *data_delay;
    int sp_id;
    int i, ret;

    s->sofa.ncid = 0;
    status = nc_open(filename, NC_NOWRITE, &ncid); /* open SOFA file read-only */
    if (status != NC_NOERR) {
        av_log(ctx, AV_LOG_ERROR, "Can't find SOFA-file '%s'\n", filename);
        return AVERROR(EINVAL);
    }

    /* get number of dimensions, vars, global attributes and Id of unlimited dimensions: */
    nc_inq(ncid, &n_dims, &n_vars, &n_gatts, &n_unlim_dim_id);

    /* -- get number of measurements ("M") and length of one IR ("N") -- */
    dim_length = av_malloc_array(n_dims, sizeof(*dim_length));
    if (!dim_length) {
        nc_close(ncid);
        return AVERROR(ENOMEM);
    }

    for (i = 0; i < n_dims; i++) { /* go through all dimensions of file */
        nc_inq_dim(ncid, i, (char *)&dim_name, &dim_length[i]); /* get dimensions */
        if (!strncmp("M", (const char *)&dim_name, 1)) /* get ID of dimension "M" */
            m_dim_id = i;
        if (!strncmp("N", (const char *)&dim_name, 1)) /* get ID of dimension "N" */
            n_dim_id = i;
    }

    if ((m_dim_id == -1) || (n_dim_id == -1)) { /* dimension "M" or "N" couldn't be found */
        av_log(ctx, AV_LOG_ERROR, "Can't find required dimensions in SOFA file.\n");
        av_freep(&dim_length);
        nc_close(ncid);
        return AVERROR(EINVAL);
    }

    n_samples = dim_length[n_dim_id]; /* get length of one IR */
    m_dim     = dim_length[m_dim_id]; /* get number of measurements */

    av_freep(&dim_length);

    /* -- check file type -- */
    /* get length of attritube "Conventions" */
    status = nc_inq_attlen(ncid, NC_GLOBAL, "Conventions", &att_len);
    if (status != NC_NOERR) {
        av_log(ctx, AV_LOG_ERROR, "Can't get length of attribute \"Conventions\".\n");
        nc_close(ncid);
        return AVERROR_INVALIDDATA;
    }

    /* check whether file is SOFA file */
    text = av_malloc(att_len + 1);
    if (!text) {
        nc_close(ncid);
        return AVERROR(ENOMEM);
    }

    nc_get_att_text(ncid, NC_GLOBAL, "Conventions", text);
    *(text + att_len) = 0;
    if (strncmp("SOFA", text, 4)) {
        av_log(ctx, AV_LOG_ERROR, "Not a SOFA file!\n");
        av_freep(&text);
        nc_close(ncid);
        return AVERROR(EINVAL);
    }
    av_freep(&text);

    status = nc_inq_attlen(ncid, NC_GLOBAL, "License", &att_len);
    if (status == NC_NOERR) {
        text = av_malloc(att_len + 1);
        if (text) {
            nc_get_att_text(ncid, NC_GLOBAL, "License", text);
            *(text + att_len) = 0;
            av_log(ctx, AV_LOG_INFO, "SOFA file License: %s\n", text);
            av_freep(&text);
        }
    }

    status = nc_inq_attlen(ncid, NC_GLOBAL, "SourceDescription", &att_len);
    if (status == NC_NOERR) {
        text = av_malloc(att_len + 1);
        if (text) {
            nc_get_att_text(ncid, NC_GLOBAL, "SourceDescription", text);
            *(text + att_len) = 0;
            av_log(ctx, AV_LOG_INFO, "SOFA file SourceDescription: %s\n", text);
            av_freep(&text);
        }
    }

    status = nc_inq_attlen(ncid, NC_GLOBAL, "Comment", &att_len);
    if (status == NC_NOERR) {
        text = av_malloc(att_len + 1);
        if (text) {
            nc_get_att_text(ncid, NC_GLOBAL, "Comment", text);
            *(text + att_len) = 0;
            av_log(ctx, AV_LOG_INFO, "SOFA file Comment: %s\n", text);
            av_freep(&text);
        }
    }

    status = nc_inq_attlen(ncid, NC_GLOBAL, "SOFAConventions", &att_len);
    if (status != NC_NOERR) {
        av_log(ctx, AV_LOG_ERROR, "Can't get length of attribute \"SOFAConventions\".\n");
        nc_close(ncid);
        return AVERROR_INVALIDDATA;
    }

    sofa_conventions = av_malloc(att_len + 1);
    if (!sofa_conventions) {
        nc_close(ncid);
        return AVERROR(ENOMEM);
    }

    nc_get_att_text(ncid, NC_GLOBAL, "SOFAConventions", sofa_conventions);
    *(sofa_conventions + att_len) = 0;
    if (strncmp("SimpleFreeFieldHRIR", sofa_conventions, att_len)) {
        av_log(ctx, AV_LOG_ERROR, "Not a SimpleFreeFieldHRIR file!\n");
        av_freep(&sofa_conventions);
        nc_close(ncid);
        return AVERROR(EINVAL);
    }
    av_freep(&sofa_conventions);

    /* -- get sampling rate of HRTFs -- */
    /* read ID, then value */
    status  = nc_inq_varid(ncid, "Data.SamplingRate", &samplingrate_id);
    status += nc_get_var_uint(ncid, samplingrate_id, &sample_rate);
    if (status != NC_NOERR) {
        av_log(ctx, AV_LOG_ERROR, "Couldn't read Data.SamplingRate.\n");
        nc_close(ncid);
        return AVERROR(EINVAL);
    }
    *samplingrate = sample_rate; /* remember sampling rate */

    /* -- allocate memory for one value for each measurement position: -- */
    sp_a = s->sofa.sp_a = av_malloc_array(m_dim, sizeof(float));
    sp_e = s->sofa.sp_e = av_malloc_array(m_dim, sizeof(float));
    sp_r = s->sofa.sp_r = av_malloc_array(m_dim, sizeof(float));
    /* delay and IR values required for each ear and measurement position: */
    data_delay = s->sofa.data_delay = av_calloc(m_dim, 2 * sizeof(int));
    data_ir = s->sofa.data_ir = av_calloc(m_dim * FFALIGN(n_samples, 16), sizeof(float) * 2);

    if (!data_delay || !sp_a || !sp_e || !sp_r || !data_ir) {
        /* if memory could not be allocated */
        close_sofa(&s->sofa);
        return AVERROR(ENOMEM);
    }

    /* get impulse responses (HRTFs): */
    /* get corresponding ID */
    status = nc_inq_varid(ncid, "Data.IR", &data_ir_id);
    status += nc_get_var_float(ncid, data_ir_id, data_ir); /* read and store IRs */
    if (status != NC_NOERR) {
        av_log(ctx, AV_LOG_ERROR, "Couldn't read Data.IR!\n");
        ret = AVERROR(EINVAL);
        goto error;
    }

    /* get source positions of the HRTFs in the SOFA file: */
    status  = nc_inq_varid(ncid, "SourcePosition", &sp_id); /* get corresponding ID */
    status += nc_get_vara_float(ncid, sp_id, (size_t[2]){ 0, 0 } ,
                (size_t[2]){ m_dim, 1}, sp_a); /* read & store azimuth angles */
    status += nc_get_vara_float(ncid, sp_id, (size_t[2]){ 0, 1 } ,
                (size_t[2]){ m_dim, 1}, sp_e); /* read & store elevation angles */
    status += nc_get_vara_float(ncid, sp_id, (size_t[2]){ 0, 2 } ,
                (size_t[2]){ m_dim, 1}, sp_r); /* read & store radii */
    if (status != NC_NOERR) { /* if any source position variable coudn't be read */
        av_log(ctx, AV_LOG_ERROR, "Couldn't read SourcePosition.\n");
        ret = AVERROR(EINVAL);
        goto error;
    }

    /* read Data.Delay, check for errors and fit it to data_delay */
    status  = nc_inq_varid(ncid, "Data.Delay", &data_delay_id);
    status += nc_inq_vardimid(ncid, data_delay_id, &data_delay_dim_id[0]);
    status += nc_inq_dimname(ncid, data_delay_dim_id[0], data_delay_dim_name);
    if (status != NC_NOERR) {
        av_log(ctx, AV_LOG_ERROR, "Couldn't read Data.Delay.\n");
        ret = AVERROR(EINVAL);
        goto error;
    }

    /* Data.Delay dimension check */
    /* dimension of Data.Delay is [I R]: */
    if (!strncmp(data_delay_dim_name, "I", 2)) {
        /* check 2 characters to assure string is 0-terminated after "I" */
        int delay[2]; /* delays get from SOFA file: */
        int *data_delay_r;

        av_log(ctx, AV_LOG_DEBUG, "Data.Delay has dimension [I R]\n");
        status = nc_get_var_int(ncid, data_delay_id, &delay[0]);
        if (status != NC_NOERR) {
            av_log(ctx, AV_LOG_ERROR, "Couldn't read Data.Delay\n");
            ret = AVERROR(EINVAL);
            goto error;
        }
        data_delay_r = data_delay + m_dim;
        for (i = 0; i < m_dim; i++) { /* extend given dimension [I R] to [M R] */
            /* assign constant delay value for all measurements to data_delay fields */
            data_delay[i]   = delay[0];
            data_delay_r[i] = delay[1];
        }
        /* dimension of Data.Delay is [M R] */
    } else if (!strncmp(data_delay_dim_name, "M", 2)) {
        av_log(ctx, AV_LOG_ERROR, "Data.Delay in dimension [M R]\n");
        /* get delays from SOFA file: */
        status = nc_get_var_int(ncid, data_delay_id, data_delay);
        if (status != NC_NOERR) {
            av_log(ctx, AV_LOG_ERROR, "Couldn't read Data.Delay\n");
            ret = AVERROR(EINVAL);
            goto error;
        }
    } else { /* dimension of Data.Delay is neither [I R] nor [M R] */
        av_log(ctx, AV_LOG_ERROR, "Data.Delay does not have the required dimensions [I R] or [M R].\n");
        ret = AVERROR(EINVAL);
        goto error;
    }

    /* save information in SOFA struct: */
    s->sofa.m_dim = m_dim; /* no. measurement positions */
    s->sofa.n_samples = n_samples; /* length on one IR */
    s->sofa.ncid = ncid; /* netCDF ID of SOFA file */
    nc_close(ncid); /* close SOFA file */

    av_log(ctx, AV_LOG_DEBUG, "m_dim: %d n_samples %d\n", m_dim, n_samples);

    return 0;

error:
    close_sofa(&s->sofa);
    return ret;
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
            if (layout >= (int64_t)1 << i) {
                channel_id += i;
                layout >>= i;
            }
        }
        /* reject layouts that are not a single channel */
        if (channel_id >= 64 || layout0 != (int64_t)1 << channel_id)
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

static int max_delay(struct NCSofa *sofa)
{
    int i, max = 0;

    for (i = 0; i < sofa->m_dim * 2; i++) {
        /* search maximum delay in given SOFA file */
        max = FFMAX(max, sofa->data_delay[i]);
    }

    return max;
}

static int find_m(SOFAlizerContext *s, int azim, int elev, float radius)
{
    /* get source positions and M of currently selected SOFA file */
    float *sp_a = s->sofa.sp_a; /* azimuth angle */
    float *sp_e = s->sofa.sp_e; /* elevation angle */
    float *sp_r = s->sofa.sp_r; /* radius */
    int m_dim = s->sofa.m_dim; /* no. measurements */
    int best_id = 0; /* index m currently closest to desired source pos. */
    float delta = 1000; /* offset between desired and currently best pos. */
    float current;
    int i;

    for (i = 0; i < m_dim; i++) {
        /* search through all measurements in currently selected SOFA file */
        /* distance of current to desired source position: */
        current = fabs(sp_a[i] - azim) +
                  fabs(sp_e[i] - elev) +
                  fabs(sp_r[i] - radius);
        if (current <= delta) {
            /* if current distance is smaller than smallest distance so far */
            delta = current;
            best_id = i; /* remember index */
        }
    }

    return best_id;
}

static int compensate_volume(AVFilterContext *ctx)
{
    struct SOFAlizerContext *s = ctx->priv;
    float compensate;
    float energy = 0;
    float *ir;
    int m;

    if (s->sofa.ncid) {
        /* find IR at front center position in the SOFA file (IR closest to 0°,0°,1m) */
        struct NCSofa *sofa = &s->sofa;
        m = find_m(s, 0, 0, 1);
        /* get energy of that IR and compensate volume */
        ir = sofa->data_ir + 2 * m * sofa->n_samples;
        if (sofa->n_samples & 31) {
            energy = avpriv_scalarproduct_float_c(ir, ir, sofa->n_samples);
        } else {
            energy = s->fdsp->scalarproduct_float(ir, ir, sofa->n_samples);
        }
        compensate = 256 / (sofa->n_samples * sqrt(energy));
        av_log(ctx, AV_LOG_DEBUG, "Compensate-factor: %f\n", compensate);
        ir = sofa->data_ir;
        /* apply volume compensation to IRs */
        if (sofa->n_samples & 31) {
            int i;
            for (i = 0; i < sofa->n_samples * sofa->m_dim * 2; i++) {
                ir[i] = ir[i] * compensate;
            }
        } else {
            s->fdsp->vector_fmul_scalar(ir, ir, compensate, sofa->n_samples * sofa->m_dim * 2);
            emms_c();
        }
    }

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

        *dst = 0;
        for (l = 0; l < in_channels; l++) {
            /* write current input sample to ringbuffer (for each channel) */
            *(buffer[l] + wr) = src[l];
        }

        /* loop goes through all channels to be convolved */
        for (l = 0; l < in_channels; l++) {
            const float *const bptr = buffer[l];

            if (l == s->lfe_channel) {
                /* LFE is an input channel but requires no convolution */
                /* apply gain to LFE signal and add to output buffer */
                *dst += *(buffer[s->lfe_channel] + wr) * s->gain_lfe;
                temp_ir += FFALIGN(n_samples, 16);
                continue;
            }

            /* current read position in ringbuffer: input sample write position
             * - delay for l-th ch. + diff. betw. IR length and buffer length
             * (mod buffer length) */
            read = (wr - *(delay + l) - (n_samples - 1) + buffer_length) & modulo;

            if (read + n_samples < buffer_length) {
                memcpy(temp_src, bptr + read, n_samples * sizeof(*temp_src));
            } else {
                int len = FFMIN(n_samples - (read % n_samples), buffer_length - read);

                memcpy(temp_src, bptr + read, len * sizeof(*temp_src));
                memcpy(temp_src + len, bptr, (n_samples - len) * sizeof(*temp_src));
            }

            /* multiply signal and IR, and add up the results */
            dst[0] += s->fdsp->scalarproduct_float(temp_ir, temp_src, n_samples);
            temp_ir += FFALIGN(n_samples, 16);
        }

        /* clippings counter */
        if (fabs(*dst) > 1)
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

static int load_data(AVFilterContext *ctx, int azim, int elev, float radius)
{
    struct SOFAlizerContext *s = ctx->priv;
    const int n_samples = s->sofa.n_samples;
    int n_conv = s->n_conv; /* no. channels to convolve */
    int n_fft = s->n_fft;
    int delay_l[16]; /* broadband delay for each IR */
    int delay_r[16];
    int nb_input_channels = ctx->inputs[0]->channels; /* no. input channels */
    float gain_lin = expf((s->gain - 3 * nb_input_channels) / 20 * M_LN10); /* gain - 3dB/channel */
    FFTComplex *data_hrtf_l = NULL;
    FFTComplex *data_hrtf_r = NULL;
    FFTComplex *fft_in_l = NULL;
    FFTComplex *fft_in_r = NULL;
    float *data_ir_l = NULL;
    float *data_ir_r = NULL;
    int offset = 0; /* used for faster pointer arithmetics in for-loop */
    int m[16]; /* measurement index m of IR closest to required source positions */
    int i, j, azim_orig = azim, elev_orig = elev;

    if (!s->sofa.ncid) { /* if an invalid SOFA file has been selected */
        av_log(ctx, AV_LOG_ERROR, "Selected SOFA file is invalid. Please select valid SOFA file.\n");
        return AVERROR_INVALIDDATA;
    }

    if (s->type == TIME_DOMAIN) {
        s->temp_src[0] = av_calloc(FFALIGN(n_samples, 16), sizeof(float));
        s->temp_src[1] = av_calloc(FFALIGN(n_samples, 16), sizeof(float));

        /* get temporary IR for L and R channel */
        data_ir_l = av_calloc(n_conv * FFALIGN(n_samples, 16), sizeof(*data_ir_l));
        data_ir_r = av_calloc(n_conv * FFALIGN(n_samples, 16), sizeof(*data_ir_r));
        if (!data_ir_r || !data_ir_l || !s->temp_src[0] || !s->temp_src[1]) {
            av_free(data_ir_l);
            av_free(data_ir_r);
            return AVERROR(ENOMEM);
        }
    } else {
        /* get temporary HRTF memory for L and R channel */
        data_hrtf_l = av_malloc_array(n_fft, sizeof(*data_hrtf_l) * n_conv);
        data_hrtf_r = av_malloc_array(n_fft, sizeof(*data_hrtf_r) * n_conv);
        if (!data_hrtf_r || !data_hrtf_l) {
            av_free(data_hrtf_l);
            av_free(data_hrtf_r);
            return AVERROR(ENOMEM);
        }
    }

    for (i = 0; i < s->n_conv; i++) {
        /* load and store IRs and corresponding delays */
        azim = (int)(s->speaker_azim[i] + azim_orig) % 360;
        elev = (int)(s->speaker_elev[i] + elev_orig) % 90;
        /* get id of IR closest to desired position */
        m[i] = find_m(s, azim, elev, radius);

        /* load the delays associated with the current IRs */
        delay_l[i] = *(s->sofa.data_delay + 2 * m[i]);
        delay_r[i] = *(s->sofa.data_delay + 2 * m[i] + 1);

        if (s->type == TIME_DOMAIN) {
            offset = i * FFALIGN(n_samples, 16); /* no. samples already written */
            for (j = 0; j < n_samples; j++) {
                /* load reversed IRs of the specified source position
                 * sample-by-sample for left and right ear; and apply gain */
                *(data_ir_l + offset + j) = /* left channel */
                *(s->sofa.data_ir + 2 * m[i] * n_samples + n_samples - 1 - j) * gain_lin;
                *(data_ir_r + offset + j) = /* right channel */
                *(s->sofa.data_ir + 2 * m[i] * n_samples + n_samples - 1 - j  + n_samples) * gain_lin;
            }
        } else {
            fft_in_l = av_calloc(n_fft, sizeof(*fft_in_l));
            fft_in_r = av_calloc(n_fft, sizeof(*fft_in_r));
            if (!fft_in_l || !fft_in_r) {
                av_free(data_hrtf_l);
                av_free(data_hrtf_r);
                av_free(fft_in_l);
                av_free(fft_in_r);
                return AVERROR(ENOMEM);
            }

            offset = i * n_fft; /* no. samples already written */
            for (j = 0; j < n_samples; j++) {
                /* load non-reversed IRs of the specified source position
                 * sample-by-sample and apply gain,
                 * L channel is loaded to real part, R channel to imag part,
                 * IRs ared shifted by L and R delay */
                fft_in_l[delay_l[i] + j].re = /* left channel */
                *(s->sofa.data_ir + 2 * m[i] * n_samples + j) * gain_lin;
                fft_in_r[delay_r[i] + j].re = /* right channel */
                *(s->sofa.data_ir + (2 * m[i] + 1) * n_samples + j) * gain_lin;
            }

            /* actually transform to frequency domain (IRs -> HRTFs) */
            av_fft_permute(s->fft[0], fft_in_l);
            av_fft_calc(s->fft[0], fft_in_l);
            memcpy(data_hrtf_l + offset, fft_in_l, n_fft * sizeof(*fft_in_l));
            av_fft_permute(s->fft[0], fft_in_r);
            av_fft_calc(s->fft[0], fft_in_r);
            memcpy(data_hrtf_r + offset, fft_in_r, n_fft * sizeof(*fft_in_r));
        }

        av_log(ctx, AV_LOG_DEBUG, "Index: %d, Azimuth: %f, Elevation: %f, Radius: %f of SOFA file.\n",
               m[i], *(s->sofa.sp_a + m[i]), *(s->sofa.sp_e + m[i]), *(s->sofa.sp_r + m[i]));
    }

    if (s->type == TIME_DOMAIN) {
        /* copy IRs and delays to allocated memory in the SOFAlizerContext struct: */
        memcpy(s->data_ir[0], data_ir_l, sizeof(float) * n_conv * FFALIGN(n_samples, 16));
        memcpy(s->data_ir[1], data_ir_r, sizeof(float) * n_conv * FFALIGN(n_samples, 16));

        av_freep(&data_ir_l); /* free temporary IR memory */
        av_freep(&data_ir_r);
    } else {
        s->data_hrtf[0] = av_malloc_array(n_fft * s->n_conv, sizeof(FFTComplex));
        s->data_hrtf[1] = av_malloc_array(n_fft * s->n_conv, sizeof(FFTComplex));
        if (!s->data_hrtf[0] || !s->data_hrtf[1]) {
            av_freep(&data_hrtf_l);
            av_freep(&data_hrtf_r);
            av_freep(&fft_in_l);
            av_freep(&fft_in_r);
            return AVERROR(ENOMEM); /* memory allocation failed */
        }

        memcpy(s->data_hrtf[0], data_hrtf_l, /* copy HRTF data to */
            sizeof(FFTComplex) * n_conv * n_fft); /* filter struct */
        memcpy(s->data_hrtf[1], data_hrtf_r,
            sizeof(FFTComplex) * n_conv * n_fft);

        av_freep(&data_hrtf_l); /* free temporary HRTF memory */
        av_freep(&data_hrtf_r);

        av_freep(&fft_in_l); /* free temporary FFT memory */
        av_freep(&fft_in_r);
    }

    memcpy(s->delay[0], &delay_l[0], sizeof(int) * s->n_conv);
    memcpy(s->delay[1], &delay_r[0], sizeof(int) * s->n_conv);

    return 0;
}

static av_cold int init(AVFilterContext *ctx)
{
    SOFAlizerContext *s = ctx->priv;
    int ret;

    if (!s->filename) {
        av_log(ctx, AV_LOG_ERROR, "Valid SOFA filename must be set.\n");
        return AVERROR(EINVAL);
    }

    /* load SOFA file, */
    /* initialize file IDs to 0 before attempting to load SOFA files,
     * this assures that in case of error, only the memory of already
     * loaded files is free'd */
    s->sofa.ncid = 0;
    ret = load_sofa(ctx, s->filename, &s->sample_rate);
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
    int nb_input_channels = inlink->channels; /* no. input channels */
    int n_max_ir = 0;
    int n_current;
    int n_max = 0;
    int ret;

    if (s->type == FREQUENCY_DOMAIN) {
        inlink->partial_buf_size =
        inlink->min_samples =
        inlink->max_samples = inlink->sample_rate;
    }

    /* gain -3 dB per channel, -6 dB to get LFE on a similar level */
    s->gain_lfe = expf((s->gain - 3 * inlink->channels - 6) / 20 * M_LN10);

    s->n_conv = nb_input_channels;

    /* get size of ringbuffer (longest IR plus max. delay) */
    /* then choose next power of 2 for performance optimization */
    n_current = s->sofa.n_samples + max_delay(&s->sofa);
    if (n_current > n_max) {
        /* length of longest IR plus max. delay (in all SOFA files) */
        n_max = n_current;
        /* length of longest IR (without delay, in all SOFA files) */
        n_max_ir = s->sofa.n_samples;
    }
    /* buffer length is longest IR plus max. delay -> next power of 2
       (32 - count leading zeros gives required exponent)  */
    s->buffer_length = 1 << (32 - ff_clz(n_max));
    s->n_fft         = 1 << (32 - ff_clz(n_max + inlink->sample_rate));

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
            return AVERROR(ENOMEM);
        }
    }

    /* Allocate memory for the impulse responses, delays and the ringbuffers */
    /* size: (longest IR) * (number of channels to convolute) */
    s->data_ir[0] = av_calloc(FFALIGN(n_max_ir, 16), sizeof(float) * s->n_conv);
    s->data_ir[1] = av_calloc(FFALIGN(n_max_ir, 16), sizeof(float) * s->n_conv);
    /* length:  number of channels to convolute */
    s->delay[0] = av_malloc_array(s->n_conv, sizeof(float));
    s->delay[1] = av_malloc_array(s->n_conv, sizeof(float));
    /* length: (buffer length) * (number of input channels),
     * OR: buffer length (if frequency domain processing)
     * calloc zero-initializes the buffer */

    if (s->type == TIME_DOMAIN) {
        s->ringbuffer[0] = av_calloc(s->buffer_length, sizeof(float) * nb_input_channels);
        s->ringbuffer[1] = av_calloc(s->buffer_length, sizeof(float) * nb_input_channels);
    } else {
        s->ringbuffer[0] = av_calloc(s->buffer_length, sizeof(float));
        s->ringbuffer[1] = av_calloc(s->buffer_length, sizeof(float));
        s->temp_fft[0] = av_malloc_array(s->n_fft, sizeof(FFTComplex));
        s->temp_fft[1] = av_malloc_array(s->n_fft, sizeof(FFTComplex));
        if (!s->temp_fft[0] || !s->temp_fft[1])
            return AVERROR(ENOMEM);
    }

    /* length: number of channels to convolute */
    s->speaker_azim = av_calloc(s->n_conv, sizeof(*s->speaker_azim));
    s->speaker_elev = av_calloc(s->n_conv, sizeof(*s->speaker_elev));

    /* memory allocation failed: */
    if (!s->data_ir[0] || !s->data_ir[1] || !s->delay[1] ||
        !s->delay[0] || !s->ringbuffer[0] || !s->ringbuffer[1] ||
        !s->speaker_azim || !s->speaker_elev)
        return AVERROR(ENOMEM);

    compensate_volume(ctx);

    /* get speaker positions */
    if ((ret = get_speaker_pos(ctx, s->speaker_azim, s->speaker_elev)) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Couldn't get speaker positions. Input channel configuration not supported.\n");
        return ret;
    }

    /* load IRs to data_ir[0] and data_ir[1] for required directions */
    if ((ret = load_data(ctx, s->rotation, s->elevation, s->radius)) < 0)
        return ret;

    av_log(ctx, AV_LOG_DEBUG, "Samplerate: %d Channels to convolute: %d, Length of ringbuffer: %d x %d\n",
        inlink->sample_rate, s->n_conv, nb_input_channels, s->buffer_length);

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    SOFAlizerContext *s = ctx->priv;

    if (s->sofa.ncid) {
        av_freep(&s->sofa.sp_a);
        av_freep(&s->sofa.sp_e);
        av_freep(&s->sofa.sp_r);
        av_freep(&s->sofa.data_delay);
        av_freep(&s->sofa.data_ir);
    }
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
