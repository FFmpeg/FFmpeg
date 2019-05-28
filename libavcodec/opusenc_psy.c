/*
 * Opus encoder
 * Copyright (c) 2017 Rostislav Pehlivanov <atomnuker@gmail.com>
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

#include "opusenc_psy.h"
#include "opus_pvq.h"
#include "opustab.h"
#include "mdct15.h"
#include "libavutil/qsort.h"

static float pvq_band_cost(CeltPVQ *pvq, CeltFrame *f, OpusRangeCoder *rc, int band,
                           float *bits, float lambda)
{
    int i, b = 0;
    uint32_t cm[2] = { (1 << f->blocks) - 1, (1 << f->blocks) - 1 };
    const int band_size = ff_celt_freq_range[band] << f->size;
    float buf[176 * 2], lowband_scratch[176], norm1[176], norm2[176];
    float dist, cost, err_x = 0.0f, err_y = 0.0f;
    float *X = buf;
    float *X_orig = f->block[0].coeffs + (ff_celt_freq_bands[band] << f->size);
    float *Y = (f->channels == 2) ? &buf[176] : NULL;
    float *Y_orig = f->block[1].coeffs + (ff_celt_freq_bands[band] << f->size);
    OPUS_RC_CHECKPOINT_SPAWN(rc);

    memcpy(X, X_orig, band_size*sizeof(float));
    if (Y)
        memcpy(Y, Y_orig, band_size*sizeof(float));

    f->remaining2 = ((f->framebits << 3) - f->anticollapse_needed) - opus_rc_tell_frac(rc) - 1;
    if (band <= f->coded_bands - 1) {
        int curr_balance = f->remaining / FFMIN(3, f->coded_bands - band);
        b = av_clip_uintp2(FFMIN(f->remaining2 + 1, f->pulses[band] + curr_balance), 14);
    }

    if (f->dual_stereo) {
        pvq->quant_band(pvq, f, rc, band, X, NULL, band_size, b / 2, f->blocks, NULL,
                        f->size, norm1, 0, 1.0f, lowband_scratch, cm[0]);

        pvq->quant_band(pvq, f, rc, band, Y, NULL, band_size, b / 2, f->blocks, NULL,
                        f->size, norm2, 0, 1.0f, lowband_scratch, cm[1]);
    } else {
        pvq->quant_band(pvq, f, rc, band, X, Y, band_size, b, f->blocks, NULL, f->size,
                        norm1, 0, 1.0f, lowband_scratch, cm[0] | cm[1]);
    }

    for (i = 0; i < band_size; i++) {
        err_x += (X[i] - X_orig[i])*(X[i] - X_orig[i]);
        if (Y)
            err_y += (Y[i] - Y_orig[i])*(Y[i] - Y_orig[i]);
    }

    dist = sqrtf(err_x) + sqrtf(err_y);
    cost = OPUS_RC_CHECKPOINT_BITS(rc)/8.0f;
    *bits += cost;

    OPUS_RC_CHECKPOINT_ROLLBACK(rc);

    return lambda*dist*cost;
}

/* Populate metrics without taking into consideration neighbouring steps */
static void step_collect_psy_metrics(OpusPsyContext *s, int index)
{
    int silence = 0, ch, i, j;
    OpusPsyStep *st = s->steps[index];

    st->index = index;

    for (ch = 0; ch < s->avctx->ch_layout.nb_channels; ch++) {
        const int lap_size = (1 << s->bsize_analysis);
        for (i = 1; i <= FFMIN(lap_size, index); i++) {
            const int offset = i*120;
            AVFrame *cur = ff_bufqueue_peek(s->bufqueue, index - i);
            memcpy(&s->scratch[offset], cur->extended_data[ch], cur->nb_samples*sizeof(float));
        }
        for (i = 0; i < lap_size; i++) {
            const int offset = i*120 + lap_size;
            AVFrame *cur = ff_bufqueue_peek(s->bufqueue, index + i);
            memcpy(&s->scratch[offset], cur->extended_data[ch], cur->nb_samples*sizeof(float));
        }

        s->dsp->vector_fmul(s->scratch, s->scratch, s->window[s->bsize_analysis],
                            (OPUS_BLOCK_SIZE(s->bsize_analysis) << 1));

        s->mdct[s->bsize_analysis]->mdct(s->mdct[s->bsize_analysis], st->coeffs[ch], s->scratch, 1);

        for (i = 0; i < CELT_MAX_BANDS; i++)
            st->bands[ch][i] = &st->coeffs[ch][ff_celt_freq_bands[i] << s->bsize_analysis];
    }

    for (ch = 0; ch < s->avctx->ch_layout.nb_channels; ch++) {
        for (i = 0; i < CELT_MAX_BANDS; i++) {
            float avg_c_s, energy = 0.0f, dist_dev = 0.0f;
            const int range = ff_celt_freq_range[i] << s->bsize_analysis;
            const float *coeffs = st->bands[ch][i];
            for (j = 0; j < range; j++)
                energy += coeffs[j]*coeffs[j];

            st->energy[ch][i] += sqrtf(energy);
            silence |= !!st->energy[ch][i];
            avg_c_s = energy / range;

            for (j = 0; j < range; j++) {
                const float c_s = coeffs[j]*coeffs[j];
                dist_dev += (avg_c_s - c_s)*(avg_c_s - c_s);
            }

            st->tone[ch][i] += sqrtf(dist_dev);
        }
    }

    st->silence = !silence;

    if (s->avctx->ch_layout.nb_channels > 1) {
        for (i = 0; i < CELT_MAX_BANDS; i++) {
            float incompat = 0.0f;
            const float *coeffs1 = st->bands[0][i];
            const float *coeffs2 = st->bands[1][i];
            const int range = ff_celt_freq_range[i] << s->bsize_analysis;
            for (j = 0; j < range; j++)
                incompat += (coeffs1[j] - coeffs2[j])*(coeffs1[j] - coeffs2[j]);
            st->stereo[i] = sqrtf(incompat);
        }
    }

    for (ch = 0; ch < s->avctx->ch_layout.nb_channels; ch++) {
        for (i = 0; i < CELT_MAX_BANDS; i++) {
            OpusBandExcitation *ex = &s->ex[ch][i];
            float bp_e = bessel_filter(&s->bfilter_lo[ch][i], st->energy[ch][i]);
            bp_e = bessel_filter(&s->bfilter_hi[ch][i], bp_e);
            bp_e *= bp_e;
            if (bp_e > ex->excitation) {
                st->change_amp[ch][i] = bp_e - ex->excitation;
                st->total_change += st->change_amp[ch][i];
                ex->excitation = ex->excitation_init = bp_e;
                ex->excitation_dist = 0.0f;
            }
            if (ex->excitation > 0.0f) {
                ex->excitation -= av_clipf((1/expf(ex->excitation_dist)), ex->excitation_init/20, ex->excitation_init/1.09);
                ex->excitation = FFMAX(ex->excitation, 0.0f);
                ex->excitation_dist += 1.0f;
            }
        }
    }
}

static void search_for_change_points(OpusPsyContext *s, float tgt_change,
                                     int offset_s, int offset_e, int resolution,
                                     int level)
{
    int i;
    float c_change = 0.0f;
    if ((offset_e - offset_s) <= resolution)
        return;
    for (i = offset_s; i < offset_e; i++) {
        c_change += s->steps[i]->total_change;
        if (c_change > tgt_change)
            break;
    }
    if (i == offset_e)
        return;
    search_for_change_points(s, tgt_change / 2.0f, offset_s, i + 0, resolution, level + 1);
    s->inflection_points[s->inflection_points_count++] = i;
    search_for_change_points(s, tgt_change / 2.0f, i + 1, offset_e, resolution, level + 1);
}

static int flush_silent_frames(OpusPsyContext *s)
{
    int fsize, silent_frames;

    for (silent_frames = 0; silent_frames < s->buffered_steps; silent_frames++)
        if (!s->steps[silent_frames]->silence)
            break;
    if (--silent_frames < 0)
        return 0;

    for (fsize = CELT_BLOCK_960; fsize > CELT_BLOCK_120; fsize--) {
        if ((1 << fsize) > silent_frames)
            continue;
        s->p.frames = FFMIN(silent_frames / (1 << fsize), 48 >> fsize);
        s->p.framesize = fsize;
        return 1;
    }

    return 0;
}

/* Main function which decides frame size and frames per current packet */
static void psy_output_groups(OpusPsyContext *s)
{
    int max_delay_samples = (s->options->max_delay_ms*s->avctx->sample_rate)/1000;
    int max_bsize = FFMIN(OPUS_SAMPLES_TO_BLOCK_SIZE(max_delay_samples), CELT_BLOCK_960);

    /* These don't change for now */
    s->p.mode      = OPUS_MODE_CELT;
    s->p.bandwidth = OPUS_BANDWIDTH_FULLBAND;

    /* Flush silent frames ASAP */
    if (s->steps[0]->silence && flush_silent_frames(s))
        return;

    s->p.framesize = FFMIN(max_bsize, CELT_BLOCK_960);
    s->p.frames    = 1;
}

int ff_opus_psy_process(OpusPsyContext *s, OpusPacketInfo *p)
{
    int i;
    float total_energy_change = 0.0f;

    if (s->buffered_steps < s->max_steps && !s->eof) {
        const int awin = (1 << s->bsize_analysis);
        if (++s->steps_to_process >= awin) {
            step_collect_psy_metrics(s, s->buffered_steps - awin + 1);
            s->steps_to_process = 0;
        }
        if ((++s->buffered_steps) < s->max_steps)
            return 1;
    }

    for (i = 0; i < s->buffered_steps; i++)
        total_energy_change += s->steps[i]->total_change;

    search_for_change_points(s, total_energy_change / 2.0f, 0,
                             s->buffered_steps, 1, 0);

    psy_output_groups(s);

    p->frames    = s->p.frames;
    p->framesize = s->p.framesize;
    p->mode      = s->p.mode;
    p->bandwidth = s->p.bandwidth;

    return 0;
}

void ff_opus_psy_celt_frame_init(OpusPsyContext *s, CeltFrame *f, int index)
{
    int i, neighbouring_points = 0, start_offset = 0;
    int radius = (1 << s->p.framesize), step_offset = radius*index;
    int silence = 1;

    f->start_band = (s->p.mode == OPUS_MODE_HYBRID) ? 17 : 0;
    f->end_band   = ff_celt_band_end[s->p.bandwidth];
    f->channels   = s->avctx->ch_layout.nb_channels;
    f->size       = s->p.framesize;

    for (i = 0; i < (1 << f->size); i++)
        silence &= s->steps[index*(1 << f->size) + i]->silence;

    f->silence = silence;
    if (f->silence) {
        f->framebits = 0; /* Otherwise the silence flag eats up 16(!) bits */
        return;
    }

    for (i = 0; i < s->inflection_points_count; i++) {
        if (s->inflection_points[i] >= step_offset) {
            start_offset = i;
            break;
        }
    }

    for (i = start_offset; i < FFMIN(radius, s->inflection_points_count - start_offset); i++) {
        if (s->inflection_points[i] < (step_offset + radius)) {
            neighbouring_points++;
        }
    }

    /* Transient flagging */
    f->transient = neighbouring_points > 0;
    f->blocks = f->transient ? OPUS_BLOCK_SIZE(s->p.framesize)/CELT_OVERLAP : 1;

    /* Some sane defaults */
    f->pfilter   = 0;
    f->pf_gain   = 0.5f;
    f->pf_octave = 2;
    f->pf_period = 1;
    f->pf_tapset = 2;

    /* More sane defaults */
    f->tf_select = 0;
    f->anticollapse = 1;
    f->alloc_trim = 5;
    f->skip_band_floor = f->end_band;
    f->intensity_stereo = f->end_band;
    f->dual_stereo = 0;
    f->spread = CELT_SPREAD_NORMAL;
    memset(f->tf_change, 0, sizeof(int)*CELT_MAX_BANDS);
    memset(f->alloc_boost, 0, sizeof(int)*CELT_MAX_BANDS);
}

static void celt_gauge_psy_weight(OpusPsyContext *s, OpusPsyStep **start,
                                  CeltFrame *f_out)
{
    int i, f, ch;
    int frame_size = OPUS_BLOCK_SIZE(s->p.framesize);
    float rate, frame_bits = 0;

    /* Used for the global ROTATE flag */
    float tonal = 0.0f;

    /* Pseudo-weights */
    float band_score[CELT_MAX_BANDS] = { 0 };
    float max_score = 1.0f;

    /* Pass one - one loop around each band, computing unquant stuff */
    for (i = 0; i < CELT_MAX_BANDS; i++) {
        float weight = 0.0f;
        float tonal_contrib = 0.0f;
        for (f = 0; f < (1 << s->p.framesize); f++) {
            weight = start[f]->stereo[i];
            for (ch = 0; ch < s->avctx->ch_layout.nb_channels; ch++) {
                weight += start[f]->change_amp[ch][i] + start[f]->tone[ch][i] + start[f]->energy[ch][i];
                tonal_contrib += start[f]->tone[ch][i];
            }
        }
        tonal += tonal_contrib;
        band_score[i] = weight;
    }

    tonal /= (float)CELT_MAX_BANDS;

    for (i = 0; i < CELT_MAX_BANDS; i++) {
        if (band_score[i] > max_score)
            max_score = band_score[i];
    }

    for (i = 0; i < CELT_MAX_BANDS; i++) {
        f_out->alloc_boost[i] = (int)((band_score[i]/max_score)*3.0f);
        frame_bits += band_score[i]*8.0f;
    }

    tonal /= 1333136.0f;
    f_out->spread = av_clip_uintp2(lrintf(tonal), 2);

    rate = ((float)s->avctx->bit_rate) + frame_bits*frame_size*16;
    rate *= s->lambda;
    rate /= s->avctx->sample_rate/frame_size;

    f_out->framebits = lrintf(rate);
    f_out->framebits = FFMIN(f_out->framebits, OPUS_MAX_PACKET_SIZE*8);
    f_out->framebits = FFALIGN(f_out->framebits, 8);
}

static int bands_dist(OpusPsyContext *s, CeltFrame *f, float *total_dist)
{
    int i, tdist = 0.0f;
    OpusRangeCoder dump;

    ff_opus_rc_enc_init(&dump);
    ff_celt_bitalloc(f, &dump, 1);

    for (i = 0; i < CELT_MAX_BANDS; i++) {
        float bits = 0.0f;
        float dist = pvq_band_cost(f->pvq, f, &dump, i, &bits, s->lambda);
        tdist += dist;
    }

    *total_dist = tdist;

    return 0;
}

static void celt_search_for_dual_stereo(OpusPsyContext *s, CeltFrame *f)
{
    float td1, td2;
    f->dual_stereo = 0;

    if (s->avctx->ch_layout.nb_channels < 2)
        return;

    bands_dist(s, f, &td1);
    f->dual_stereo = 1;
    bands_dist(s, f, &td2);

    f->dual_stereo = td2 < td1;
    s->dual_stereo_used += td2 < td1;
}

static void celt_search_for_intensity(OpusPsyContext *s, CeltFrame *f)
{
    int i, best_band = CELT_MAX_BANDS - 1;
    float dist, best_dist = FLT_MAX;
    /* TODO: fix, make some heuristic up here using the lambda value */
    float end_band = 0;

    if (s->avctx->ch_layout.nb_channels < 2)
        return;

    for (i = f->end_band; i >= end_band; i--) {
        f->intensity_stereo = i;
        bands_dist(s, f, &dist);
        if (best_dist > dist) {
            best_dist = dist;
            best_band = i;
        }
    }

    f->intensity_stereo = best_band;
    s->avg_is_band = (s->avg_is_band + f->intensity_stereo)/2.0f;
}

static int celt_search_for_tf(OpusPsyContext *s, OpusPsyStep **start, CeltFrame *f)
{
    int i, j, k, cway, config[2][CELT_MAX_BANDS] = { { 0 } };
    float score[2] = { 0 };

    for (cway = 0; cway < 2; cway++) {
        int mag[2];
        int base = f->transient ? 120 : 960;

        for (i = 0; i < 2; i++) {
            int c = ff_celt_tf_select[f->size][f->transient][cway][i];
            mag[i] = c < 0 ? base >> FFABS(c) : base << FFABS(c);
        }

        for (i = 0; i < CELT_MAX_BANDS; i++) {
            float iscore0 = 0.0f;
            float iscore1 = 0.0f;
            for (j = 0; j < (1 << f->size); j++) {
                for (k = 0; k < s->avctx->ch_layout.nb_channels; k++) {
                    iscore0 += start[j]->tone[k][i]*start[j]->change_amp[k][i]/mag[0];
                    iscore1 += start[j]->tone[k][i]*start[j]->change_amp[k][i]/mag[1];
                }
            }
            config[cway][i] = FFABS(iscore0 - 1.0f) < FFABS(iscore1 - 1.0f);
            score[cway] += config[cway][i] ? iscore1 : iscore0;
        }
    }

    f->tf_select = score[0] < score[1];
    memcpy(f->tf_change, config[f->tf_select], sizeof(int)*CELT_MAX_BANDS);

    return 0;
}

int ff_opus_psy_celt_frame_process(OpusPsyContext *s, CeltFrame *f, int index)
{
    int start_transient_flag = f->transient;
    OpusPsyStep **start = &s->steps[index * (1 << s->p.framesize)];

    if (f->silence)
        return 0;

    celt_gauge_psy_weight(s, start, f);
    celt_search_for_intensity(s, f);
    celt_search_for_dual_stereo(s, f);
    celt_search_for_tf(s, start, f);

    if (f->transient != start_transient_flag) {
        f->blocks = f->transient ? OPUS_BLOCK_SIZE(s->p.framesize)/CELT_OVERLAP : 1;
        s->redo_analysis = 1;
        return 1;
    }

    s->redo_analysis = 0;

    return 0;
}

void ff_opus_psy_postencode_update(OpusPsyContext *s, CeltFrame *f, OpusRangeCoder *rc)
{
    int i, frame_size = OPUS_BLOCK_SIZE(s->p.framesize);
    int steps_out = s->p.frames*(frame_size/120);
    void *tmp[FF_BUFQUEUE_SIZE];
    float ideal_fbits;

    for (i = 0; i < steps_out; i++)
        memset(s->steps[i], 0, sizeof(OpusPsyStep));

    for (i = 0; i < s->max_steps; i++)
        tmp[i] = s->steps[i];

    for (i = 0; i < s->max_steps; i++) {
        const int i_new = i - steps_out;
        s->steps[i_new < 0 ? s->max_steps + i_new : i_new] = tmp[i];
    }

    for (i = steps_out; i < s->buffered_steps; i++)
        s->steps[i]->index -= steps_out;

    ideal_fbits = s->avctx->bit_rate/(s->avctx->sample_rate/frame_size);

    for (i = 0; i < s->p.frames; i++) {
        s->avg_is_band += f[i].intensity_stereo;
        s->lambda *= ideal_fbits / f[i].framebits;
    }

    s->avg_is_band /= (s->p.frames + 1);

    s->cs_num = 0;
    s->steps_to_process = 0;
    s->buffered_steps -= steps_out;
    s->total_packets_out += s->p.frames;
    s->inflection_points_count = 0;
}

av_cold int ff_opus_psy_init(OpusPsyContext *s, AVCodecContext *avctx,
                             struct FFBufQueue *bufqueue, OpusEncOptions *options)
{
    int i, ch, ret;

    s->redo_analysis = 0;
    s->lambda = 1.0f;
    s->options = options;
    s->avctx = avctx;
    s->bufqueue = bufqueue;
    s->max_steps = ceilf(s->options->max_delay_ms/2.5f);
    s->bsize_analysis = CELT_BLOCK_960;
    s->avg_is_band = CELT_MAX_BANDS - 1;
    s->inflection_points_count = 0;

    s->inflection_points = av_mallocz(sizeof(*s->inflection_points)*s->max_steps);
    if (!s->inflection_points) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    s->dsp = avpriv_float_dsp_alloc(avctx->flags & AV_CODEC_FLAG_BITEXACT);
    if (!s->dsp) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    for (ch = 0; ch < s->avctx->ch_layout.nb_channels; ch++) {
        for (i = 0; i < CELT_MAX_BANDS; i++) {
            bessel_init(&s->bfilter_hi[ch][i], 1.0f, 19.0f, 100.0f, 1);
            bessel_init(&s->bfilter_lo[ch][i], 1.0f, 20.0f, 100.0f, 0);
        }
    }

    for (i = 0; i < s->max_steps; i++) {
        s->steps[i] = av_mallocz(sizeof(OpusPsyStep));
        if (!s->steps[i]) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    }

    for (i = 0; i < CELT_BLOCK_NB; i++) {
        float tmp;
        const int len = OPUS_BLOCK_SIZE(i);
        s->window[i] = av_malloc(2*len*sizeof(float));
        if (!s->window[i]) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        generate_window_func(s->window[i], 2*len, WFUNC_SINE, &tmp);
        if ((ret = ff_mdct15_init(&s->mdct[i], 0, i + 3, 68 << (CELT_BLOCK_NB - 1 - i))))
            goto fail;
    }

    return 0;

fail:
    av_freep(&s->inflection_points);
    av_freep(&s->dsp);

    for (i = 0; i < CELT_BLOCK_NB; i++) {
        ff_mdct15_uninit(&s->mdct[i]);
        av_freep(&s->window[i]);
    }

    for (i = 0; i < s->max_steps; i++)
        av_freep(&s->steps[i]);

    return ret;
}

void ff_opus_psy_signal_eof(OpusPsyContext *s)
{
    s->eof = 1;
}

av_cold int ff_opus_psy_end(OpusPsyContext *s)
{
    int i;

    av_freep(&s->inflection_points);
    av_freep(&s->dsp);

    for (i = 0; i < CELT_BLOCK_NB; i++) {
        ff_mdct15_uninit(&s->mdct[i]);
        av_freep(&s->window[i]);
    }

    for (i = 0; i < s->max_steps; i++)
        av_freep(&s->steps[i]);

    av_log(s->avctx, AV_LOG_INFO, "Average Intensity Stereo band: %0.1f\n", s->avg_is_band);
    av_log(s->avctx, AV_LOG_INFO, "Dual Stereo used: %0.2f%%\n", ((float)s->dual_stereo_used/s->total_packets_out)*100.0f);

    return 0;
}
