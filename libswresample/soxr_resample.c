/*
 * audio resampling with soxr
 * Copyright (c) 2012 Rob Sykes <robs@users.sourceforge.net>
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
 * audio resampling with soxr
 */

#include "libavutil/log.h"
#include "swresample_internal.h"

#include <soxr.h>

static struct ResampleContext *create(struct ResampleContext *c, int out_rate, int in_rate, int filter_size, int phase_shift, int linear,
        double cutoff, enum AVSampleFormat format, enum SwrFilterType filter_type, int kaiser_beta, double precision, int cheby){
    soxr_error_t error;

    soxr_datatype_t type =
        format == AV_SAMPLE_FMT_S16P? SOXR_INT16_S :
        format == AV_SAMPLE_FMT_S16 ? SOXR_INT16_I :
        format == AV_SAMPLE_FMT_S32P? SOXR_INT32_S :
        format == AV_SAMPLE_FMT_S32 ? SOXR_INT32_I :
        format == AV_SAMPLE_FMT_FLTP? SOXR_FLOAT32_S :
        format == AV_SAMPLE_FMT_FLT ? SOXR_FLOAT32_I :
        format == AV_SAMPLE_FMT_DBLP? SOXR_FLOAT64_S :
        format == AV_SAMPLE_FMT_DBL ? SOXR_FLOAT64_I : (soxr_datatype_t)-1;

    soxr_io_spec_t io_spec = soxr_io_spec(type, type);

    soxr_quality_spec_t q_spec = soxr_quality_spec((int)((precision-2)/4), (SOXR_HI_PREC_CLOCK|SOXR_ROLLOFF_NONE)*!!cheby);
    q_spec.precision = linear? 0 : precision;
#if !defined SOXR_VERSION /* Deprecated @ March 2013: */
    q_spec.bw_pc = cutoff? FFMAX(FFMIN(cutoff,.995),.8)*100 : q_spec.bw_pc;
#else
    q_spec.passband_end = cutoff? FFMAX(FFMIN(cutoff,.995),.8) : q_spec.passband_end;
#endif

    soxr_delete((soxr_t)c);
    c = (struct ResampleContext *)
        soxr_create(in_rate, out_rate, 0, &error, &io_spec, &q_spec, 0);
    if (!c)
        av_log(NULL, AV_LOG_ERROR, "soxr_create: %s\n", error);
    return c;
}

static void destroy(struct ResampleContext * *c){
    soxr_delete((soxr_t)*c);
    *c = NULL;
}

static int flush(struct SwrContext *s){
    soxr_process((soxr_t)s->resample, NULL, 0, NULL, NULL, 0, NULL);
    return 0;
}

static int process(
        struct ResampleContext * c, AudioData *dst, int dst_size,
        AudioData *src, int src_size, int *consumed){
    size_t idone, odone;
    soxr_error_t error = soxr_set_error((soxr_t)c, soxr_set_num_channels((soxr_t)c, src->ch_count));
    error = soxr_process((soxr_t)c, src->ch, (size_t)src_size,
            &idone, dst->ch, (size_t)dst_size, &odone);
    *consumed = (int)idone;
    return error? -1 : odone;
}

static int64_t get_delay(struct SwrContext *s, int64_t base){
    double delay_s = soxr_delay((soxr_t)s->resample) / s->out_sample_rate;
    return (int64_t)(delay_s * base + .5);
}

struct Resampler const soxr_resampler={
    create, destroy, process, flush, NULL /* set_compensation */, get_delay,
};

