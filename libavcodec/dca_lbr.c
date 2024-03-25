/*
 * Copyright (C) 2016 foo86
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

#define BITSTREAM_READER_LE

#include "libavutil/channel_layout.h"
#include "libavutil/mem.h"
#include "libavutil/mem_internal.h"

#include "dcadec.h"
#include "dcadata.h"
#include "dcahuff.h"
#include "dca_syncwords.h"
#include "bytestream.h"
#include "decode.h"

#define AMP_MAX     56

enum LBRFlags {
    LBR_FLAG_24_BIT             = 0x01,
    LBR_FLAG_LFE_PRESENT        = 0x02,
    LBR_FLAG_BAND_LIMIT_2_3     = 0x04,
    LBR_FLAG_BAND_LIMIT_1_2     = 0x08,
    LBR_FLAG_BAND_LIMIT_1_3     = 0x0c,
    LBR_FLAG_BAND_LIMIT_1_4     = 0x10,
    LBR_FLAG_BAND_LIMIT_1_8     = 0x18,
    LBR_FLAG_BAND_LIMIT_NONE    = 0x14,
    LBR_FLAG_BAND_LIMIT_MASK    = 0x1c,
    LBR_FLAG_DMIX_STEREO        = 0x20,
    LBR_FLAG_DMIX_MULTI_CH      = 0x40
};

enum LBRChunkTypes {
    LBR_CHUNK_NULL              = 0x00,
    LBR_CHUNK_PAD               = 0x01,
    LBR_CHUNK_FRAME             = 0x04,
    LBR_CHUNK_FRAME_NO_CSUM     = 0x06,
    LBR_CHUNK_LFE               = 0x0a,
    LBR_CHUNK_ECS               = 0x0b,
    LBR_CHUNK_RESERVED_1        = 0x0c,
    LBR_CHUNK_RESERVED_2        = 0x0d,
    LBR_CHUNK_SCF               = 0x0e,
    LBR_CHUNK_TONAL             = 0x10,
    LBR_CHUNK_TONAL_GRP_1       = 0x11,
    LBR_CHUNK_TONAL_GRP_2       = 0x12,
    LBR_CHUNK_TONAL_GRP_3       = 0x13,
    LBR_CHUNK_TONAL_GRP_4       = 0x14,
    LBR_CHUNK_TONAL_GRP_5       = 0x15,
    LBR_CHUNK_TONAL_SCF         = 0x16,
    LBR_CHUNK_TONAL_SCF_GRP_1   = 0x17,
    LBR_CHUNK_TONAL_SCF_GRP_2   = 0x18,
    LBR_CHUNK_TONAL_SCF_GRP_3   = 0x19,
    LBR_CHUNK_TONAL_SCF_GRP_4   = 0x1a,
    LBR_CHUNK_TONAL_SCF_GRP_5   = 0x1b,
    LBR_CHUNK_RES_GRID_LR       = 0x30,
    LBR_CHUNK_RES_GRID_LR_LAST  = 0x3f,
    LBR_CHUNK_RES_GRID_HR       = 0x40,
    LBR_CHUNK_RES_GRID_HR_LAST  = 0x4f,
    LBR_CHUNK_RES_TS_1          = 0x50,
    LBR_CHUNK_RES_TS_1_LAST     = 0x5f,
    LBR_CHUNK_RES_TS_2          = 0x60,
    LBR_CHUNK_RES_TS_2_LAST     = 0x6f,
    LBR_CHUNK_EXTENSION         = 0x7f
};

typedef struct LBRChunk {
    int id, len;
    const uint8_t *data;
} LBRChunk;

static const int8_t channel_reorder_nolfe[7][5] = {
    { 0, -1, -1, -1, -1 },  // C
    { 0,  1, -1, -1, -1 },  // LR
    { 0,  1,  2, -1, -1 },  // LR C
    { 0,  1, -1, -1, -1 },  // LsRs
    { 1,  2,  0, -1, -1 },  // LsRs C
    { 0,  1,  2,  3, -1 },  // LR LsRs
    { 0,  1,  3,  4,  2 },  // LR LsRs C
};

static const int8_t channel_reorder_lfe[7][5] = {
    { 0, -1, -1, -1, -1 },  // C
    { 0,  1, -1, -1, -1 },  // LR
    { 0,  1,  2, -1, -1 },  // LR C
    { 1,  2, -1, -1, -1 },  // LsRs
    { 2,  3,  0, -1, -1 },  // LsRs C
    { 0,  1,  3,  4, -1 },  // LR LsRs
    { 0,  1,  4,  5,  2 },  // LR LsRs C
};

static const uint8_t lfe_index[7] = {
    1, 2, 3, 0, 1, 2, 3
};

static const uint16_t channel_layouts[7] = {
    AV_CH_LAYOUT_MONO,
    AV_CH_LAYOUT_STEREO,
    AV_CH_LAYOUT_SURROUND,
    AV_CH_SIDE_LEFT | AV_CH_SIDE_RIGHT,
    AV_CH_FRONT_CENTER | AV_CH_SIDE_LEFT | AV_CH_SIDE_RIGHT,
    AV_CH_LAYOUT_2_2,
    AV_CH_LAYOUT_5POINT0
};

static float    cos_tab[256];
static const float lpc_tab[16] = {
    /* lpc_tab[i] = sin((i - 8) * (M_PI / ((i < 8) ? 17 : 15))) */
    -0.995734176295034521871191178905, -0.961825643172819070408796290732,
    -0.895163291355062322067016499754, -0.798017227280239503332805112796,
    -0.673695643646557211712691912426, -0.526432162877355800244607799141,
    -0.361241666187152948744714596184, -0.183749517816570331574408839621,
     0.0,                               0.207911690817759337101742284405,
     0.406736643075800207753985990341,  0.587785252292473129168705954639,
     0.743144825477394235014697048974,  0.866025403784438646763723170753,
     0.951056516295153572116439333379,  0.994521895368273336922691944981
};

av_cold void ff_dca_lbr_init_tables(void)
{
    int i;

    for (i = 0; i < 256; i++)
        cos_tab[i] = cos(M_PI * i / 128);
}

static int parse_lfe_24(DCALbrDecoder *s)
{
    int step_max = FF_ARRAY_ELEMS(ff_dca_lfe_step_size_24) - 1;
    int i, ps, si, code, step_i;
    float step, value, delta;

    ps = get_bits(&s->gb, 24);
    si = ps >> 23;

    value = (((ps & 0x7fffff) ^ -si) + si) * (1.0f / 0x7fffff);

    step_i = get_bits(&s->gb, 8);
    if (step_i > step_max) {
        av_log(s->avctx, AV_LOG_ERROR, "Invalid LFE step size index\n");
        return AVERROR_INVALIDDATA;
    }

    step = ff_dca_lfe_step_size_24[step_i];

    for (i = 0; i < 64; i++) {
        code = get_bits(&s->gb, 6);

        delta = step * 0.03125f;
        if (code & 16)
            delta += step;
        if (code & 8)
            delta += step * 0.5f;
        if (code & 4)
            delta += step * 0.25f;
        if (code & 2)
            delta += step * 0.125f;
        if (code & 1)
            delta += step * 0.0625f;

        if (code & 32) {
            value -= delta;
            if (value < -3.0f)
                value = -3.0f;
        } else {
            value += delta;
            if (value > 3.0f)
                value = 3.0f;
        }

        step_i += ff_dca_lfe_delta_index_24[code & 31];
        step_i = av_clip(step_i, 0, step_max);

        step = ff_dca_lfe_step_size_24[step_i];
        s->lfe_data[i] = value * s->lfe_scale;
    }

    return 0;
}

static int parse_lfe_16(DCALbrDecoder *s)
{
    int step_max = FF_ARRAY_ELEMS(ff_dca_lfe_step_size_16) - 1;
    int i, ps, si, code, step_i;
    float step, value, delta;

    ps = get_bits(&s->gb, 16);
    si = ps >> 15;

    value = (((ps & 0x7fff) ^ -si) + si) * (1.0f / 0x7fff);

    step_i = get_bits(&s->gb, 8);
    if (step_i > step_max) {
        av_log(s->avctx, AV_LOG_ERROR, "Invalid LFE step size index\n");
        return AVERROR_INVALIDDATA;
    }

    step = ff_dca_lfe_step_size_16[step_i];

    for (i = 0; i < 64; i++) {
        code = get_bits(&s->gb, 4);

        delta = step * 0.125f;
        if (code & 4)
            delta += step;
        if (code & 2)
            delta += step * 0.5f;
        if (code & 1)
            delta += step * 0.25f;

        if (code & 8) {
            value -= delta;
            if (value < -3.0f)
                value = -3.0f;
        } else {
            value += delta;
            if (value > 3.0f)
                value = 3.0f;
        }

        step_i += ff_dca_lfe_delta_index_16[code & 7];
        step_i = av_clip(step_i, 0, step_max);

        step = ff_dca_lfe_step_size_16[step_i];
        s->lfe_data[i] = value * s->lfe_scale;
    }

    return 0;
}

static int parse_lfe_chunk(DCALbrDecoder *s, LBRChunk *chunk)
{
    int ret;

    if (!(s->flags & LBR_FLAG_LFE_PRESENT))
        return 0;

    if (!chunk->len)
        return 0;

    ret = init_get_bits8(&s->gb, chunk->data, chunk->len);
    if (ret < 0)
        return ret;

    // Determine bit depth from chunk size
    if (chunk->len >= 52)
        return parse_lfe_24(s);
    if (chunk->len >= 35)
        return parse_lfe_16(s);

    av_log(s->avctx, AV_LOG_ERROR, "LFE chunk too short\n");
    return AVERROR_INVALIDDATA;
}

static inline int parse_vlc(GetBitContext *s, const VLC *vlc,
                            int nb_bits, int max_depth)
{
    int v = get_vlc2(s, vlc->table, nb_bits, max_depth);
    if (v >= 0)
        return v;
    // Rare value
    return get_bits(s, get_bits(s, 3) + 1);
}

static int parse_tonal(DCALbrDecoder *s, int group)
{
    unsigned int amp[DCA_LBR_CHANNELS_TOTAL];
    unsigned int phs[DCA_LBR_CHANNELS_TOTAL];
    unsigned int diff, main_amp, shift;
    int sf, sf_idx, ch, main_ch, freq;
    int ch_nbits = av_ceil_log2(s->nchannels_total);

    // Parse subframes for this group
    for (sf = 0; sf < 1 << group; sf += diff ? 8 : 1) {
        sf_idx = ((s->framenum << group) + sf) & 31;
        s->tonal_bounds[group][sf_idx][0] = s->ntones;

        // Parse tones for this subframe
        for (freq = 1;; freq++) {
            if (get_bits_left(&s->gb) < 1) {
                av_log(s->avctx, AV_LOG_ERROR, "Tonal group chunk too short\n");
                return AVERROR_INVALIDDATA;
            }

            diff = parse_vlc(&s->gb, &ff_dca_vlc_tnl_grp[group], DCA_TNL_GRP_VLC_BITS, 2);
            if (diff >= FF_ARRAY_ELEMS(ff_dca_fst_amp)) {
                av_log(s->avctx, AV_LOG_ERROR, "Invalid tonal frequency diff\n");
                return AVERROR_INVALIDDATA;
            }

            diff = get_bitsz(&s->gb, diff >> 2) + ff_dca_fst_amp[diff];
            if (diff <= 1)
                break;  // End of subframe

            freq += diff - 2;
            if (freq >> (5 - group) > s->nsubbands * 4 - 6) {
                av_log(s->avctx, AV_LOG_ERROR, "Invalid spectral line offset\n");
                return AVERROR_INVALIDDATA;
            }

            // Main channel
            main_ch = get_bitsz(&s->gb, ch_nbits);
            main_amp = parse_vlc(&s->gb, &ff_dca_vlc_tnl_scf, DCA_TNL_SCF_VLC_BITS, 2)
                + s->tonal_scf[ff_dca_freq_to_sb[freq >> (7 - group)]]
                + s->limited_range - 2;
            amp[main_ch] = main_amp < AMP_MAX ? main_amp : 0;
            phs[main_ch] = get_bits(&s->gb, 3);

            // Secondary channels
            for (ch = 0; ch < s->nchannels_total; ch++) {
                if (ch == main_ch)
                    continue;
                if (get_bits1(&s->gb)) {
                    amp[ch] = amp[main_ch] - parse_vlc(&s->gb, &ff_dca_vlc_damp, DCA_DAMP_VLC_BITS, 1);
                    phs[ch] = phs[main_ch] - parse_vlc(&s->gb, &ff_dca_vlc_dph,  DCA_DPH_VLC_BITS,  1);
                } else {
                    amp[ch] = 0;
                    phs[ch] = 0;
                }
            }

            if (amp[main_ch]) {
                // Allocate new tone
                DCALbrTone *t = &s->tones[s->ntones];
                s->ntones = (s->ntones + 1) & (DCA_LBR_TONES - 1);

                t->x_freq = freq >> (5 - group);
                t->f_delt = (freq & ((1 << (5 - group)) - 1)) << group;
                t->ph_rot = 256 - (t->x_freq & 1) * 128 - t->f_delt * 4;

                shift = ff_dca_ph0_shift[(t->x_freq & 3) * 2 + (freq & 1)]
                    - ((t->ph_rot << (5 - group)) - t->ph_rot);

                for (ch = 0; ch < s->nchannels; ch++) {
                    t->amp[ch] = amp[ch] < AMP_MAX ? amp[ch] : 0;
                    t->phs[ch] = 128 - phs[ch] * 32 + shift;
                }
            }
        }

        s->tonal_bounds[group][sf_idx][1] = s->ntones;
    }

    return 0;
}

static int parse_tonal_chunk(DCALbrDecoder *s, LBRChunk *chunk)
{
    int sb, group, ret;

    if (!chunk->len)
        return 0;

    ret = init_get_bits8(&s->gb, chunk->data, chunk->len);

    if (ret < 0)
        return ret;

    // Scale factors
    if (chunk->id == LBR_CHUNK_SCF || chunk->id == LBR_CHUNK_TONAL_SCF) {
        if (get_bits_left(&s->gb) < 36) {
            av_log(s->avctx, AV_LOG_ERROR, "Tonal scale factor chunk too short\n");
            return AVERROR_INVALIDDATA;
        }
        for (sb = 0; sb < 6; sb++)
            s->tonal_scf[sb] = get_bits(&s->gb, 6);
    }

    // Tonal groups
    if (chunk->id == LBR_CHUNK_TONAL || chunk->id == LBR_CHUNK_TONAL_SCF)
        for (group = 0; group < 5; group++) {
            ret = parse_tonal(s, group);
            if (ret < 0)
                return ret;
        }

    return 0;
}

static int parse_tonal_group(DCALbrDecoder *s, LBRChunk *chunk)
{
    int ret;

    if (!chunk->len)
        return 0;

    ret = init_get_bits8(&s->gb, chunk->data, chunk->len);
    if (ret < 0)
        return ret;

    return parse_tonal(s, chunk->id);
}

/**
 * Check point to ensure that enough bits are left. Aborts decoding
 * by skipping to the end of chunk otherwise.
 */
static int ensure_bits(GetBitContext *s, int n)
{
    int left = get_bits_left(s);
    if (left < 0)
        return AVERROR_INVALIDDATA;
    if (left < n) {
        skip_bits_long(s, left);
        return 1;
    }
    return 0;
}

static int parse_scale_factors(DCALbrDecoder *s, uint8_t *scf)
{
    int i, sf, prev, next, dist;

    // Truncated scale factors remain zero
    if (ensure_bits(&s->gb, 20))
        return 0;

    // Initial scale factor
    prev = parse_vlc(&s->gb, &ff_dca_vlc_fst_rsd_amp, DCA_FST_RSD_VLC_BITS, 2);

    for (sf = 0; sf < 7; sf += dist) {
        scf[sf] = prev; // Store previous value

        if (ensure_bits(&s->gb, 20))
            return 0;

        // Interpolation distance
        dist = parse_vlc(&s->gb, &ff_dca_vlc_rsd_apprx, DCA_RSD_APPRX_VLC_BITS, 1) + 1;
        if (dist > 7 - sf) {
            av_log(s->avctx, AV_LOG_ERROR, "Invalid scale factor distance\n");
            return AVERROR_INVALIDDATA;
        }

        if (ensure_bits(&s->gb, 20))
            return 0;

        // Final interpolation point
        next = parse_vlc(&s->gb, &ff_dca_vlc_rsd_amp, DCA_RSD_AMP_VLC_BITS, 2);

        if (next & 1)
            next = prev + ((next + 1) >> 1);
        else
            next = prev - ( next      >> 1);

        // Interpolate
        switch (dist) {
        case 2:
            if (next > prev)
                scf[sf + 1] = prev + ((next - prev) >> 1);
            else
                scf[sf + 1] = prev - ((prev - next) >> 1);
            break;

        case 4:
            if (next > prev) {
                scf[sf + 1] = prev + ( (next - prev)      >> 2);
                scf[sf + 2] = prev + ( (next - prev)      >> 1);
                scf[sf + 3] = prev + (((next - prev) * 3) >> 2);
            } else {
                scf[sf + 1] = prev - ( (prev - next)      >> 2);
                scf[sf + 2] = prev - ( (prev - next)      >> 1);
                scf[sf + 3] = prev - (((prev - next) * 3) >> 2);
            }
            break;

        default:
            for (i = 1; i < dist; i++)
                scf[sf + i] = prev + (next - prev) * i / dist;
            break;
        }

        prev = next;
    }

    scf[sf] = next; // Store final value

    return 0;
}

static int parse_st_code(GetBitContext *s, int min_v)
{
    unsigned int v = parse_vlc(s, &ff_dca_vlc_st_grid, DCA_ST_GRID_VLC_BITS, 2) + min_v;

    if (v & 1)
        v = 16 + (v >> 1);
    else
        v = 16 - (v >> 1);

    if (v >= FF_ARRAY_ELEMS(ff_dca_st_coeff))
        v = 16;
    return v;
}

static int parse_grid_1_chunk(DCALbrDecoder *s, LBRChunk *chunk, int ch1, int ch2)
{
    int ch, sb, sf, nsubbands, ret;

    if (!chunk->len)
        return 0;

    ret = init_get_bits8(&s->gb, chunk->data, chunk->len);
    if (ret < 0)
        return ret;

    // Scale factors
    nsubbands = ff_dca_scf_to_grid_1[s->nsubbands - 1] + 1;
    for (sb = 2; sb < nsubbands; sb++) {
        ret = parse_scale_factors(s, s->grid_1_scf[ch1][sb]);
        if (ret < 0)
            return ret;
        if (ch1 != ch2 && ff_dca_grid_1_to_scf[sb] < s->min_mono_subband) {
            ret = parse_scale_factors(s, s->grid_1_scf[ch2][sb]);
            if (ret < 0)
                return ret;
        }
    }

    if (get_bits_left(&s->gb) < 1)
        return 0;   // Should not happen, but a sample exists that proves otherwise

    // Average values for third grid
    for (sb = 0; sb < s->nsubbands - 4; sb++) {
        s->grid_3_avg[ch1][sb] = parse_vlc(&s->gb, &ff_dca_vlc_avg_g3, DCA_AVG_G3_VLC_BITS, 2) - 16;
        if (ch1 != ch2) {
            if (sb + 4 < s->min_mono_subband)
                s->grid_3_avg[ch2][sb] = parse_vlc(&s->gb, &ff_dca_vlc_avg_g3, DCA_AVG_G3_VLC_BITS, 2) - 16;
            else
                s->grid_3_avg[ch2][sb] = s->grid_3_avg[ch1][sb];
        }
    }

    if (get_bits_left(&s->gb) < 0) {
        av_log(s->avctx, AV_LOG_ERROR, "First grid chunk too short\n");
        return AVERROR_INVALIDDATA;
    }

    // Stereo image for partial mono mode
    if (ch1 != ch2) {
        int min_v[2];

        if (ensure_bits(&s->gb, 8))
            return 0;

        min_v[0] = get_bits(&s->gb, 4);
        min_v[1] = get_bits(&s->gb, 4);

        nsubbands = (s->nsubbands - s->min_mono_subband + 3) / 4;
        for (sb = 0; sb < nsubbands; sb++)
            for (ch = ch1; ch <= ch2; ch++)
                for (sf = 1; sf <= 4; sf++)
                    s->part_stereo[ch][sb][sf] = parse_st_code(&s->gb, min_v[ch - ch1]);

        if (get_bits_left(&s->gb) >= 0)
            s->part_stereo_pres |= 1 << ch1;
    }

    // Low resolution spatial information is not decoded

    return 0;
}

static int parse_grid_1_sec_ch(DCALbrDecoder *s, int ch2)
{
    int sb, nsubbands, ret;

    // Scale factors
    nsubbands = ff_dca_scf_to_grid_1[s->nsubbands - 1] + 1;
    for (sb = 2; sb < nsubbands; sb++) {
        if (ff_dca_grid_1_to_scf[sb] >= s->min_mono_subband) {
            ret = parse_scale_factors(s, s->grid_1_scf[ch2][sb]);
            if (ret < 0)
                return ret;
        }
    }

    // Average values for third grid
    for (sb = 0; sb < s->nsubbands - 4; sb++) {
        if (sb + 4 >= s->min_mono_subband) {
            if (ensure_bits(&s->gb, 20))
                return 0;
            s->grid_3_avg[ch2][sb] = parse_vlc(&s->gb, &ff_dca_vlc_avg_g3, DCA_AVG_G3_VLC_BITS, 2) - 16;
        }
    }

    return 0;
}

static void parse_grid_3(DCALbrDecoder *s, int ch1, int ch2, int sb, int flag)
{
    int i, ch;

    for (ch = ch1; ch <= ch2; ch++) {
        if ((ch != ch1 && sb + 4 >= s->min_mono_subband) != flag)
            continue;

        if (s->grid_3_pres[ch] & (1U << sb))
            continue;   // Already parsed

        for (i = 0; i < 8; i++) {
            if (ensure_bits(&s->gb, 20))
                return;
            s->grid_3_scf[ch][sb][i] = parse_vlc(&s->gb, &ff_dca_vlc_grid_3, DCA_GRID_VLC_BITS, 2) - 16;
        }

        // Flag scale factors for this subband parsed
        s->grid_3_pres[ch] |= 1U << sb;
    }
}

static float lbr_rand(DCALbrDecoder *s, int sb)
{
    s->lbr_rand = 1103515245U * s->lbr_rand + 12345U;
    return s->lbr_rand * s->sb_scf[sb];
}

/**
 * Parse time samples for one subband, filling truncated samples with randomness
 */
static void parse_ch(DCALbrDecoder *s, int ch, int sb, int quant_level, int flag)
{
    float *samples = s->time_samples[ch][sb];
    int i, j, code, nblocks, coding_method;

    if (ensure_bits(&s->gb, 20))
        return; // Too few bits left

    coding_method = get_bits1(&s->gb);

    switch (quant_level) {
    case 1:
        nblocks = FFMIN(get_bits_left(&s->gb) / 8, DCA_LBR_TIME_SAMPLES / 8);
        for (i = 0; i < nblocks; i++, samples += 8) {
            code = get_bits(&s->gb, 8);
            for (j = 0; j < 8; j++)
                samples[j] = ff_dca_rsd_level_2a[(code >> j) & 1];
        }
        i = nblocks * 8;
        break;

    case 2:
        if (coding_method) {
            for (i = 0; i < DCA_LBR_TIME_SAMPLES && get_bits_left(&s->gb) >= 2; i++) {
                if (get_bits1(&s->gb))
                    samples[i] = ff_dca_rsd_level_2b[get_bits1(&s->gb)];
                else
                    samples[i] = 0;
            }
        } else {
            nblocks = FFMIN(get_bits_left(&s->gb) / 8, (DCA_LBR_TIME_SAMPLES + 4) / 5);
            for (i = 0; i < nblocks; i++, samples += 5) {
                code = ff_dca_rsd_pack_5_in_8[get_bits(&s->gb, 8)];
                for (j = 0; j < 5; j++)
                    samples[j] = ff_dca_rsd_level_3[(code >> j * 2) & 3];
            }
            i = nblocks * 5;
        }
        break;

    case 3:
        nblocks = FFMIN(get_bits_left(&s->gb) / 7, (DCA_LBR_TIME_SAMPLES + 2) / 3);
        for (i = 0; i < nblocks; i++, samples += 3) {
            code = get_bits(&s->gb, 7);
            for (j = 0; j < 3; j++)
                samples[j] = ff_dca_rsd_level_5[ff_dca_rsd_pack_3_in_7[code][j]];
        }
        i = nblocks * 3;
        break;

    case 4:
        for (i = 0; i < DCA_LBR_TIME_SAMPLES && get_bits_left(&s->gb) >= 6; i++)
            samples[i] = ff_dca_rsd_level_8[get_vlc2(&s->gb, ff_dca_vlc_rsd.table, 6, 1)];
        break;

    case 5:
        nblocks = FFMIN(get_bits_left(&s->gb) / 4, DCA_LBR_TIME_SAMPLES);
        for (i = 0; i < nblocks; i++)
            samples[i] = ff_dca_rsd_level_16[get_bits(&s->gb, 4)];
        break;

    default:
        av_assert0(0);
    }

    if (flag && get_bits_left(&s->gb) < 20)
        return; // Skip incomplete mono subband

    for (; i < DCA_LBR_TIME_SAMPLES; i++)
        s->time_samples[ch][sb][i] = lbr_rand(s, sb);

    s->ch_pres[ch] |= 1U << sb;
}

static int parse_ts(DCALbrDecoder *s, int ch1, int ch2,
                    int start_sb, int end_sb, int flag)
{
    int sb, sb_g3, sb_reorder, quant_level;

    for (sb = start_sb; sb < end_sb; sb++) {
        // Subband number before reordering
        if (sb < 6) {
            sb_reorder = sb;
        } else if (flag && sb < s->max_mono_subband) {
            sb_reorder = s->sb_indices[sb];
        } else {
            if (ensure_bits(&s->gb, 28))
                break;
            sb_reorder = get_bits(&s->gb, s->limited_range + 3);
            if (sb_reorder < 6)
                sb_reorder = 6;
            s->sb_indices[sb] = sb_reorder;
        }
        if (sb_reorder >= s->nsubbands)
            return AVERROR_INVALIDDATA;

        // Third grid scale factors
        if (sb == 12) {
            for (sb_g3 = 0; sb_g3 < s->g3_avg_only_start_sb - 4; sb_g3++)
                parse_grid_3(s, ch1, ch2, sb_g3, flag);
        } else if (sb < 12 && sb_reorder >= 4) {
            parse_grid_3(s, ch1, ch2, sb_reorder - 4, flag);
        }

        // Secondary channel flags
        if (ch1 != ch2) {
            if (ensure_bits(&s->gb, 20))
                break;
            if (!flag || sb_reorder >= s->max_mono_subband)
                s->sec_ch_sbms[ch1 / 2][sb_reorder] = get_bits(&s->gb, 8);
            if (flag && sb_reorder >= s->min_mono_subband)
                s->sec_ch_lrms[ch1 / 2][sb_reorder] = get_bits(&s->gb, 8);
        }

        quant_level = s->quant_levels[ch1 / 2][sb];
        if (!quant_level)
            return AVERROR_INVALIDDATA;

        // Time samples for one or both channels
        if (sb < s->max_mono_subband && sb_reorder >= s->min_mono_subband) {
            if (!flag)
                parse_ch(s, ch1, sb_reorder, quant_level, 0);
            else if (ch1 != ch2)
                parse_ch(s, ch2, sb_reorder, quant_level, 1);
        } else {
            parse_ch(s, ch1, sb_reorder, quant_level, 0);
            if (ch1 != ch2)
                parse_ch(s, ch2, sb_reorder, quant_level, 0);
        }
    }

    return 0;
}

/**
 * Convert from reflection coefficients to direct form coefficients
 */
static void convert_lpc(float *coeff, const int *codes)
{
    int i, j;

    for (i = 0; i < 8; i++) {
        float rc = lpc_tab[codes[i]];
        for (j = 0; j < (i + 1) / 2; j++) {
            float tmp1 = coeff[    j    ];
            float tmp2 = coeff[i - j - 1];
            coeff[    j    ] = tmp1 + rc * tmp2;
            coeff[i - j - 1] = tmp2 + rc * tmp1;
        }
        coeff[i] = rc;
    }
}

static int parse_lpc(DCALbrDecoder *s, int ch1, int ch2, int start_sb, int end_sb)
{
    int f = s->framenum & 1;
    int i, sb, ch, codes[16];

    // First two subbands have two sets of coefficients, third subband has one
    for (sb = start_sb; sb < end_sb; sb++) {
        int ncodes = 8 * (1 + (sb < 2));
        for (ch = ch1; ch <= ch2; ch++) {
            if (ensure_bits(&s->gb, 4 * ncodes))
                return 0;
            for (i = 0; i < ncodes; i++)
                codes[i] = get_bits(&s->gb, 4);
            for (i = 0; i < ncodes / 8; i++)
                convert_lpc(s->lpc_coeff[f][ch][sb][i], &codes[i * 8]);
        }
    }

    return 0;
}

static int parse_high_res_grid(DCALbrDecoder *s, LBRChunk *chunk, int ch1, int ch2)
{
    int quant_levels[DCA_LBR_SUBBANDS];
    int sb, ch, ol, st, max_sb, profile, ret;

    if (!chunk->len)
        return 0;

    ret = init_get_bits8(&s->gb, chunk->data, chunk->len);
    if (ret < 0)
        return ret;

    // Quantizer profile
    profile = get_bits(&s->gb, 8);
    // Overall level
    ol = (profile >> 3) & 7;
    // Steepness
    st = profile >> 6;
    // Max energy subband
    max_sb = profile & 7;

    // Calculate quantization levels
    for (sb = 0; sb < s->nsubbands; sb++) {
        int f = sb * s->limited_rate / s->nsubbands;
        int a = 18000 / (12 * f / 1000 + 100 + 40 * st) + 20 * ol;
        if (a <= 95)
            quant_levels[sb] = 1;
        else if (a <= 140)
            quant_levels[sb] = 2;
        else if (a <= 180)
            quant_levels[sb] = 3;
        else if (a <= 230)
            quant_levels[sb] = 4;
        else
            quant_levels[sb] = 5;
    }

    // Reorder quantization levels for lower subbands
    for (sb = 0; sb < 8; sb++)
        s->quant_levels[ch1 / 2][sb] = quant_levels[ff_dca_sb_reorder[max_sb][sb]];
    for (; sb < s->nsubbands; sb++)
        s->quant_levels[ch1 / 2][sb] = quant_levels[sb];

    // LPC for the first two subbands
    ret = parse_lpc(s, ch1, ch2, 0, 2);
    if (ret < 0)
        return ret;

    // Time-samples for the first two subbands of main channel
    ret = parse_ts(s, ch1, ch2, 0, 2, 0);
    if (ret < 0)
        return ret;

    // First two bands of the first grid
    for (sb = 0; sb < 2; sb++)
        for (ch = ch1; ch <= ch2; ch++)
            if ((ret = parse_scale_factors(s, s->grid_1_scf[ch][sb])) < 0)
                return ret;

    return 0;
}

static int parse_grid_2(DCALbrDecoder *s, int ch1, int ch2,
                        int start_sb, int end_sb, int flag)
{
    int i, j, sb, ch, nsubbands;

    nsubbands = ff_dca_scf_to_grid_2[s->nsubbands - 1] + 1;
    if (end_sb > nsubbands)
        end_sb = nsubbands;

    for (sb = start_sb; sb < end_sb; sb++) {
        for (ch = ch1; ch <= ch2; ch++) {
            uint8_t *g2_scf = s->grid_2_scf[ch][sb];

            if ((ch != ch1 && ff_dca_grid_2_to_scf[sb] >= s->min_mono_subband) != flag) {
                if (!flag)
                    memcpy(g2_scf, s->grid_2_scf[ch1][sb], 64);
                continue;
            }

            // Scale factors in groups of 8
            for (i = 0; i < 8; i++, g2_scf += 8) {
                if (get_bits_left(&s->gb) < 1) {
                    memset(g2_scf, 0, 64 - i * 8);
                    break;
                }
                // Bit indicating if whole group has zero values
                if (get_bits1(&s->gb)) {
                    for (j = 0; j < 8; j++) {
                        if (ensure_bits(&s->gb, 20))
                            break;
                        g2_scf[j] = parse_vlc(&s->gb, &ff_dca_vlc_grid_2, DCA_GRID_VLC_BITS, 2);
                    }
                } else {
                    memset(g2_scf, 0, 8);
                }
            }
        }
    }

    return 0;
}

static int parse_ts1_chunk(DCALbrDecoder *s, LBRChunk *chunk, int ch1, int ch2)
{
    int ret;
    if (!chunk->len)
        return 0;
    if ((ret = init_get_bits8(&s->gb, chunk->data, chunk->len)) < 0)
        return ret;
    if ((ret = parse_lpc(s, ch1, ch2, 2, 3)) < 0)
        return ret;
    if ((ret = parse_ts(s, ch1, ch2, 2, 4, 0)) < 0)
        return ret;
    if ((ret = parse_grid_2(s, ch1, ch2, 0, 1, 0)) < 0)
        return ret;
    if ((ret = parse_ts(s, ch1, ch2, 4, 6, 0)) < 0)
        return ret;
    return 0;
}

static int parse_ts2_chunk(DCALbrDecoder *s, LBRChunk *chunk, int ch1, int ch2)
{
    int ret;

    if (!chunk->len)
        return 0;
    if ((ret = init_get_bits8(&s->gb, chunk->data, chunk->len)) < 0)
        return ret;
    if ((ret = parse_grid_2(s, ch1, ch2, 1, 3, 0)) < 0)
        return ret;
    if ((ret = parse_ts(s, ch1, ch2, 6, s->max_mono_subband, 0)) < 0)
        return ret;
    if (ch1 != ch2) {
        if ((ret = parse_grid_1_sec_ch(s, ch2)) < 0)
            return ret;
        if ((ret = parse_grid_2(s, ch1, ch2, 0, 3, 1)) < 0)
            return ret;
    }
    if ((ret = parse_ts(s, ch1, ch2, s->min_mono_subband, s->nsubbands, 1)) < 0)
        return ret;
    return 0;
}

static int init_sample_rate(DCALbrDecoder *s)
{
    double scale = (-1.0 / (1 << 17)) * sqrt(1 << (2 - s->limited_range));
    float scale_t = scale;
    int i, br_per_ch = s->bit_rate_scaled / s->nchannels_total;
    int ret;

    av_tx_uninit(&s->imdct);

    ret = av_tx_init(&s->imdct, &s->imdct_fn, AV_TX_FLOAT_MDCT, 1,
                     1 << (s->freq_range + 5), &scale_t, AV_TX_FULL_IMDCT);
    if (ret < 0)
        return ret;

    for (i = 0; i < 32 << s->freq_range; i++)
        s->window[i] = ff_dca_long_window[i << (2 - s->freq_range)];

    if (br_per_ch < 14000)
        scale = 0.85;
    else if (br_per_ch < 32000)
        scale = (br_per_ch - 14000) * (1.0 / 120000) + 0.85;
    else
        scale = 1.0;

    scale *= 1.0 / INT_MAX;

    for (i = 0; i < s->nsubbands; i++) {
        if (i < 2)
            s->sb_scf[i] = 0;   // The first two subbands are always zero
        else if (i < 5)
            s->sb_scf[i] = (i - 1) * 0.25 * 0.785 * scale;
        else
            s->sb_scf[i] = 0.785 * scale;
    }

    s->lfe_scale = (16 << s->freq_range) * 0.0000078265894;

    return 0;
}

static int alloc_sample_buffer(DCALbrDecoder *s)
{
    // Reserve space for history and padding
    int nchsamples = DCA_LBR_TIME_SAMPLES + DCA_LBR_TIME_HISTORY * 2;
    int nsamples = nchsamples * s->nchannels * s->nsubbands;
    int ch, sb;
    float *ptr;

    // Reallocate time sample buffer
    av_fast_mallocz(&s->ts_buffer, &s->ts_size, nsamples * sizeof(float));
    if (!s->ts_buffer)
        return AVERROR(ENOMEM);

    ptr = s->ts_buffer + DCA_LBR_TIME_HISTORY;
    for (ch = 0; ch < s->nchannels; ch++) {
        for (sb = 0; sb < s->nsubbands; sb++) {
            s->time_samples[ch][sb] = ptr;
            ptr += nchsamples;
        }
    }

    return 0;
}

static int parse_decoder_init(DCALbrDecoder *s, GetByteContext *gb)
{
    int old_rate = s->sample_rate;
    int old_band_limit = s->band_limit;
    int old_nchannels = s->nchannels;
    int version, bit_rate_hi;
    unsigned int sr_code;

    // Sample rate of LBR audio
    sr_code = bytestream2_get_byte(gb);
    if (sr_code >= FF_ARRAY_ELEMS(ff_dca_sampling_freqs)) {
        av_log(s->avctx, AV_LOG_ERROR, "Invalid LBR sample rate\n");
        return AVERROR_INVALIDDATA;
    }
    s->sample_rate = ff_dca_sampling_freqs[sr_code];
    if (s->sample_rate > 48000) {
        avpriv_report_missing_feature(s->avctx, "%d Hz LBR sample rate", s->sample_rate);
        return AVERROR_PATCHWELCOME;
    }

    // LBR speaker mask
    s->ch_mask = bytestream2_get_le16(gb);
    if (!(s->ch_mask & 0x7)) {
        avpriv_report_missing_feature(s->avctx, "LBR channel mask %#x", s->ch_mask);
        return AVERROR_PATCHWELCOME;
    }
    if ((s->ch_mask & 0xfff0) && !(s->warned & 1)) {
        avpriv_report_missing_feature(s->avctx, "LBR channel mask %#x", s->ch_mask);
        s->warned |= 1;
    }

    // LBR bitstream version
    version = bytestream2_get_le16(gb);
    if ((version & 0xff00) != 0x0800) {
        avpriv_report_missing_feature(s->avctx, "LBR stream version %#x", version);
        return AVERROR_PATCHWELCOME;
    }

    // Flags for LBR decoder initialization
    s->flags = bytestream2_get_byte(gb);
    if (s->flags & LBR_FLAG_DMIX_MULTI_CH) {
        avpriv_report_missing_feature(s->avctx, "LBR multi-channel downmix");
        return AVERROR_PATCHWELCOME;
    }
    if ((s->flags & LBR_FLAG_LFE_PRESENT) && s->sample_rate != 48000) {
        if (!(s->warned & 2)) {
            avpriv_report_missing_feature(s->avctx, "%d Hz LFE interpolation", s->sample_rate);
            s->warned |= 2;
        }
        s->flags &= ~LBR_FLAG_LFE_PRESENT;
    }

    // Most significant bit rate nibbles
    bit_rate_hi = bytestream2_get_byte(gb);

    // Least significant original bit rate word
    s->bit_rate_orig = bytestream2_get_le16(gb) | ((bit_rate_hi & 0x0F) << 16);

    // Least significant scaled bit rate word
    s->bit_rate_scaled = bytestream2_get_le16(gb) | ((bit_rate_hi & 0xF0) << 12);

    // Setup number of fullband channels
    s->nchannels_total = ff_dca_count_chs_for_mask(s->ch_mask & ~DCA_SPEAKER_PAIR_LFE1);
    s->nchannels = FFMIN(s->nchannels_total, DCA_LBR_CHANNELS);

    // Setup band limit
    switch (s->flags & LBR_FLAG_BAND_LIMIT_MASK) {
    case LBR_FLAG_BAND_LIMIT_NONE:
        s->band_limit = 0;
        break;
    case LBR_FLAG_BAND_LIMIT_1_2:
        s->band_limit = 1;
        break;
    case LBR_FLAG_BAND_LIMIT_1_4:
        s->band_limit = 2;
        break;
    default:
        avpriv_report_missing_feature(s->avctx, "LBR band limit %#x", s->flags & LBR_FLAG_BAND_LIMIT_MASK);
        return AVERROR_PATCHWELCOME;
    }

    // Setup frequency range
    s->freq_range = ff_dca_freq_ranges[sr_code];

    // Setup resolution profile
    if (s->bit_rate_orig >= 44000 * (s->nchannels_total + 2))
        s->res_profile = 2;
    else if (s->bit_rate_orig >= 25000 * (s->nchannels_total + 2))
        s->res_profile = 1;
    else
        s->res_profile = 0;

    // Setup limited sample rate, number of subbands, etc
    s->limited_rate = s->sample_rate >> s->band_limit;
    s->limited_range = s->freq_range - s->band_limit;
    if (s->limited_range < 0) {
        av_log(s->avctx, AV_LOG_ERROR, "Invalid LBR band limit for frequency range\n");
        return AVERROR_INVALIDDATA;
    }

    s->nsubbands = 8 << s->limited_range;

    s->g3_avg_only_start_sb = s->nsubbands * ff_dca_avg_g3_freqs[s->res_profile] / (s->limited_rate / 2);
    if (s->g3_avg_only_start_sb > s->nsubbands)
        s->g3_avg_only_start_sb = s->nsubbands;

    s->min_mono_subband = s->nsubbands *  2000 / (s->limited_rate / 2);
    if (s->min_mono_subband > s->nsubbands)
        s->min_mono_subband = s->nsubbands;

    s->max_mono_subband = s->nsubbands * 14000 / (s->limited_rate / 2);
    if (s->max_mono_subband > s->nsubbands)
        s->max_mono_subband = s->nsubbands;

    // Handle change of sample rate
    if ((old_rate != s->sample_rate || old_band_limit != s->band_limit) && init_sample_rate(s) < 0)
        return AVERROR(ENOMEM);

    // Setup stereo downmix
    if (s->flags & LBR_FLAG_DMIX_STEREO) {
        DCAContext *dca = s->avctx->priv_data;

        if (s->nchannels_total < 3 || s->nchannels_total > DCA_LBR_CHANNELS_TOTAL - 2) {
            av_log(s->avctx, AV_LOG_ERROR, "Invalid number of channels for LBR stereo downmix\n");
            return AVERROR_INVALIDDATA;
        }

        // This decoder doesn't support ECS chunk
        if (dca->request_channel_layout != DCA_SPEAKER_LAYOUT_STEREO && !(s->warned & 4)) {
            avpriv_report_missing_feature(s->avctx, "Embedded LBR stereo downmix");
            s->warned |= 4;
        }

        // Account for extra downmixed channel pair
        s->nchannels_total += 2;
        s->nchannels = 2;
        s->ch_mask = DCA_SPEAKER_PAIR_LR;
        s->flags &= ~LBR_FLAG_LFE_PRESENT;
    }

    // Handle change of sample rate or number of channels
    if (old_rate != s->sample_rate
        || old_band_limit != s->band_limit
        || old_nchannels != s->nchannels) {
        if (alloc_sample_buffer(s) < 0)
            return AVERROR(ENOMEM);
        ff_dca_lbr_flush(s);
    }

    return 0;
}

int ff_dca_lbr_parse(DCALbrDecoder *s, const uint8_t *data, DCAExssAsset *asset)
{
    struct {
        LBRChunk    lfe;
        LBRChunk    tonal;
        LBRChunk    tonal_grp[5];
        LBRChunk    grid1[DCA_LBR_CHANNELS / 2];
        LBRChunk    hr_grid[DCA_LBR_CHANNELS / 2];
        LBRChunk    ts1[DCA_LBR_CHANNELS / 2];
        LBRChunk    ts2[DCA_LBR_CHANNELS / 2];
    } chunk = { {0} };

    GetByteContext gb;

    int i, ch, sb, sf, ret, group, chunk_id, chunk_len;

    bytestream2_init(&gb, data + asset->lbr_offset, asset->lbr_size);

    // LBR sync word
    if (bytestream2_get_be32(&gb) != DCA_SYNCWORD_LBR) {
        av_log(s->avctx, AV_LOG_ERROR, "Invalid LBR sync word\n");
        return AVERROR_INVALIDDATA;
    }

    // LBR header type
    switch (bytestream2_get_byte(&gb)) {
    case DCA_LBR_HEADER_SYNC_ONLY:
        if (!s->sample_rate) {
            av_log(s->avctx, AV_LOG_ERROR, "LBR decoder not initialized\n");
            return AVERROR_INVALIDDATA;
        }
        break;
    case DCA_LBR_HEADER_DECODER_INIT:
        if ((ret = parse_decoder_init(s, &gb)) < 0) {
            s->sample_rate = 0;
            return ret;
        }
        break;
    default:
        av_log(s->avctx, AV_LOG_ERROR, "Invalid LBR header type\n");
        return AVERROR_INVALIDDATA;
    }

    // LBR frame chunk header
    chunk_id = bytestream2_get_byte(&gb);
    chunk_len = (chunk_id & 0x80) ? bytestream2_get_be16(&gb) : bytestream2_get_byte(&gb);

    if (chunk_len > bytestream2_get_bytes_left(&gb)) {
        chunk_len = bytestream2_get_bytes_left(&gb);
        av_log(s->avctx, AV_LOG_WARNING, "LBR frame chunk was truncated\n");
        if (s->avctx->err_recognition & AV_EF_EXPLODE)
            return AVERROR_INVALIDDATA;
    }

    bytestream2_init(&gb, gb.buffer, chunk_len);

    switch (chunk_id & 0x7f) {
    case LBR_CHUNK_FRAME:
        if (s->avctx->err_recognition & (AV_EF_CRCCHECK | AV_EF_CAREFUL)) {
            int checksum = bytestream2_get_be16(&gb);
            uint16_t res = chunk_id;
            res += (chunk_len >> 8) & 0xff;
            res += chunk_len & 0xff;
            for (i = 0; i < chunk_len - 2; i++)
                res += gb.buffer[i];
            if (checksum != res) {
                av_log(s->avctx, AV_LOG_WARNING, "Invalid LBR checksum\n");
                if (s->avctx->err_recognition & AV_EF_EXPLODE)
                    return AVERROR_INVALIDDATA;
            }
        } else {
            bytestream2_skip(&gb, 2);
        }
        break;
    case LBR_CHUNK_FRAME_NO_CSUM:
        break;
    default:
        av_log(s->avctx, AV_LOG_ERROR, "Invalid LBR frame chunk ID\n");
        return AVERROR_INVALIDDATA;
    }

    // Clear current frame
    memset(s->quant_levels, 0, sizeof(s->quant_levels));
    memset(s->sb_indices, 0xff, sizeof(s->sb_indices));
    memset(s->sec_ch_sbms, 0, sizeof(s->sec_ch_sbms));
    memset(s->sec_ch_lrms, 0, sizeof(s->sec_ch_lrms));
    memset(s->ch_pres, 0, sizeof(s->ch_pres));
    memset(s->grid_1_scf, 0, sizeof(s->grid_1_scf));
    memset(s->grid_2_scf, 0, sizeof(s->grid_2_scf));
    memset(s->grid_3_avg, 0, sizeof(s->grid_3_avg));
    memset(s->grid_3_scf, 0, sizeof(s->grid_3_scf));
    memset(s->grid_3_pres, 0, sizeof(s->grid_3_pres));
    memset(s->tonal_scf, 0, sizeof(s->tonal_scf));
    memset(s->lfe_data, 0, sizeof(s->lfe_data));
    s->part_stereo_pres = 0;
    s->framenum = (s->framenum + 1) & 31;

    for (ch = 0; ch < s->nchannels; ch++) {
        for (sb = 0; sb < s->nsubbands / 4; sb++) {
            s->part_stereo[ch][sb][0] = s->part_stereo[ch][sb][4];
            s->part_stereo[ch][sb][4] = 16;
        }
    }

    memset(s->lpc_coeff[s->framenum & 1], 0, sizeof(s->lpc_coeff[0]));

    for (group = 0; group < 5; group++) {
        for (sf = 0; sf < 1 << group; sf++) {
            int sf_idx = ((s->framenum << group) + sf) & 31;
            s->tonal_bounds[group][sf_idx][0] =
            s->tonal_bounds[group][sf_idx][1] = s->ntones;
        }
    }

    // Parse chunk headers
    while (bytestream2_get_bytes_left(&gb) > 0) {
        chunk_id = bytestream2_get_byte(&gb);
        chunk_len = (chunk_id & 0x80) ? bytestream2_get_be16(&gb) : bytestream2_get_byte(&gb);
        chunk_id &= 0x7f;

        if (chunk_len > bytestream2_get_bytes_left(&gb)) {
            chunk_len = bytestream2_get_bytes_left(&gb);
            av_log(s->avctx, AV_LOG_WARNING, "LBR chunk %#x was truncated\n", chunk_id);
            if (s->avctx->err_recognition & AV_EF_EXPLODE)
                return AVERROR_INVALIDDATA;
        }

        switch (chunk_id) {
        case LBR_CHUNK_LFE:
            chunk.lfe.len  = chunk_len;
            chunk.lfe.data = gb.buffer;
            break;

        case LBR_CHUNK_SCF:
        case LBR_CHUNK_TONAL:
        case LBR_CHUNK_TONAL_SCF:
            chunk.tonal.id   = chunk_id;
            chunk.tonal.len  = chunk_len;
            chunk.tonal.data = gb.buffer;
            break;

        case LBR_CHUNK_TONAL_GRP_1:
        case LBR_CHUNK_TONAL_GRP_2:
        case LBR_CHUNK_TONAL_GRP_3:
        case LBR_CHUNK_TONAL_GRP_4:
        case LBR_CHUNK_TONAL_GRP_5:
            i = LBR_CHUNK_TONAL_GRP_5 - chunk_id;
            chunk.tonal_grp[i].id   = i;
            chunk.tonal_grp[i].len  = chunk_len;
            chunk.tonal_grp[i].data = gb.buffer;
            break;

        case LBR_CHUNK_TONAL_SCF_GRP_1:
        case LBR_CHUNK_TONAL_SCF_GRP_2:
        case LBR_CHUNK_TONAL_SCF_GRP_3:
        case LBR_CHUNK_TONAL_SCF_GRP_4:
        case LBR_CHUNK_TONAL_SCF_GRP_5:
            i = LBR_CHUNK_TONAL_SCF_GRP_5 - chunk_id;
            chunk.tonal_grp[i].id   = i;
            chunk.tonal_grp[i].len  = chunk_len;
            chunk.tonal_grp[i].data = gb.buffer;
            break;

        case LBR_CHUNK_RES_GRID_LR:
        case LBR_CHUNK_RES_GRID_LR + 1:
        case LBR_CHUNK_RES_GRID_LR + 2:
            i = chunk_id - LBR_CHUNK_RES_GRID_LR;
            chunk.grid1[i].len  = chunk_len;
            chunk.grid1[i].data = gb.buffer;
            break;

        case LBR_CHUNK_RES_GRID_HR:
        case LBR_CHUNK_RES_GRID_HR + 1:
        case LBR_CHUNK_RES_GRID_HR + 2:
            i = chunk_id - LBR_CHUNK_RES_GRID_HR;
            chunk.hr_grid[i].len  = chunk_len;
            chunk.hr_grid[i].data = gb.buffer;
            break;

        case LBR_CHUNK_RES_TS_1:
        case LBR_CHUNK_RES_TS_1 + 1:
        case LBR_CHUNK_RES_TS_1 + 2:
            i = chunk_id - LBR_CHUNK_RES_TS_1;
            chunk.ts1[i].len  = chunk_len;
            chunk.ts1[i].data = gb.buffer;
            break;

        case LBR_CHUNK_RES_TS_2:
        case LBR_CHUNK_RES_TS_2 + 1:
        case LBR_CHUNK_RES_TS_2 + 2:
            i = chunk_id - LBR_CHUNK_RES_TS_2;
            chunk.ts2[i].len  = chunk_len;
            chunk.ts2[i].data = gb.buffer;
            break;
        }

        bytestream2_skip(&gb, chunk_len);
    }

    // Parse the chunks
    ret = parse_lfe_chunk(s, &chunk.lfe);

    ret |= parse_tonal_chunk(s, &chunk.tonal);

    for (i = 0; i < 5; i++)
        ret |= parse_tonal_group(s, &chunk.tonal_grp[i]);

    for (i = 0; i < (s->nchannels + 1) / 2; i++) {
        int ch1 = i * 2;
        int ch2 = FFMIN(ch1 + 1, s->nchannels - 1);

        if (parse_grid_1_chunk (s, &chunk.grid1  [i], ch1, ch2) < 0 ||
            parse_high_res_grid(s, &chunk.hr_grid[i], ch1, ch2) < 0) {
            ret = -1;
            continue;
        }

        // TS chunks depend on both grids. TS_2 depends on TS_1.
        if (!chunk.grid1[i].len || !chunk.hr_grid[i].len || !chunk.ts1[i].len)
            continue;

        if (parse_ts1_chunk(s, &chunk.ts1[i], ch1, ch2) < 0 ||
            parse_ts2_chunk(s, &chunk.ts2[i], ch1, ch2) < 0) {
            ret = -1;
            continue;
        }
    }

    if (ret < 0 && (s->avctx->err_recognition & AV_EF_EXPLODE))
        return AVERROR_INVALIDDATA;

    return 0;
}

/**
 * Reconstruct high-frequency resolution grid from first and third grids
 */
static void decode_grid(DCALbrDecoder *s, int ch1, int ch2)
{
    int i, ch, sb;

    for (ch = ch1; ch <= ch2; ch++) {
        for (sb = 0; sb < s->nsubbands; sb++) {
            int g1_sb = ff_dca_scf_to_grid_1[sb];

            uint8_t *g1_scf_a = s->grid_1_scf[ch][g1_sb    ];
            uint8_t *g1_scf_b = s->grid_1_scf[ch][g1_sb + 1];

            int w1 = ff_dca_grid_1_weights[g1_sb    ][sb];
            int w2 = ff_dca_grid_1_weights[g1_sb + 1][sb];

            uint8_t *hr_scf = s->high_res_scf[ch][sb];

            if (sb < 4) {
                for (i = 0; i < 8; i++) {
                    int scf = w1 * g1_scf_a[i] + w2 * g1_scf_b[i];
                    hr_scf[i] = scf >> 7;
                }
            } else {
                int8_t *g3_scf = s->grid_3_scf[ch][sb - 4];
                int g3_avg = s->grid_3_avg[ch][sb - 4];

                for (i = 0; i < 8; i++) {
                    int scf = w1 * g1_scf_a[i] + w2 * g1_scf_b[i];
                    hr_scf[i] = (scf >> 7) - g3_avg - g3_scf[i];
                }
            }
        }
    }
}

/**
 * Fill unallocated subbands with randomness
 */
static void random_ts(DCALbrDecoder *s, int ch1, int ch2)
{
    int i, j, k, ch, sb;

    for (ch = ch1; ch <= ch2; ch++) {
        for (sb = 0; sb < s->nsubbands; sb++) {
            float *samples = s->time_samples[ch][sb];

            if (s->ch_pres[ch] & (1U << sb))
                continue;   // Skip allocated subband

            if (sb < 2) {
                // The first two subbands are always zero
                memset(samples, 0, DCA_LBR_TIME_SAMPLES * sizeof(float));
            } else if (sb < 10) {
                for (i = 0; i < DCA_LBR_TIME_SAMPLES; i++)
                    samples[i] = lbr_rand(s, sb);
            } else {
                for (i = 0; i < DCA_LBR_TIME_SAMPLES / 8; i++, samples += 8) {
                    float accum[8] = { 0 };

                    // Modulate by subbands 2-5 in blocks of 8
                    for (k = 2; k < 6; k++) {
                        float *other = &s->time_samples[ch][k][i * 8];
                        for (j = 0; j < 8; j++)
                            accum[j] += fabs(other[j]);
                    }

                    for (j = 0; j < 8; j++)
                        samples[j] = (accum[j] * 0.25f + 0.5f) * lbr_rand(s, sb);
                }
            }
        }
    }
}

static void predict(float *samples, const float *coeff, int nsamples)
{
    int i, j;

    for (i = 0; i < nsamples; i++) {
        float res = 0;
        for (j = 0; j < 8; j++)
            res += coeff[j] * samples[i - j - 1];
        samples[i] -= res;
    }
}

static void synth_lpc(DCALbrDecoder *s, int ch1, int ch2, int sb)
{
    int f = s->framenum & 1;
    int ch;

    for (ch = ch1; ch <= ch2; ch++) {
        float *samples = s->time_samples[ch][sb];

        if (!(s->ch_pres[ch] & (1U << sb)))
            continue;

        if (sb < 2) {
            predict(samples,      s->lpc_coeff[f^1][ch][sb][1],  16);
            predict(samples + 16, s->lpc_coeff[f  ][ch][sb][0],  64);
            predict(samples + 80, s->lpc_coeff[f  ][ch][sb][1],  48);
        } else {
            predict(samples,      s->lpc_coeff[f^1][ch][sb][0],  16);
            predict(samples + 16, s->lpc_coeff[f  ][ch][sb][0], 112);
        }
    }
}

static void filter_ts(DCALbrDecoder *s, int ch1, int ch2)
{
    int i, j, sb, ch;

    for (sb = 0; sb < s->nsubbands; sb++) {
        // Scale factors
        for (ch = ch1; ch <= ch2; ch++) {
            float *samples = s->time_samples[ch][sb];
            uint8_t *hr_scf = s->high_res_scf[ch][sb];
            if (sb < 4) {
                for (i = 0; i < DCA_LBR_TIME_SAMPLES / 16; i++, samples += 16) {
                    unsigned int scf = hr_scf[i];
                    if (scf > AMP_MAX)
                        scf = AMP_MAX;
                    for (j = 0; j < 16; j++)
                        samples[j] *= ff_dca_quant_amp[scf];
                }
            } else {
                uint8_t *g2_scf = s->grid_2_scf[ch][ff_dca_scf_to_grid_2[sb]];
                for (i = 0; i < DCA_LBR_TIME_SAMPLES / 2; i++, samples += 2) {
                    unsigned int scf = hr_scf[i / 8] - g2_scf[i];
                    if (scf > AMP_MAX)
                        scf = AMP_MAX;
                    samples[0] *= ff_dca_quant_amp[scf];
                    samples[1] *= ff_dca_quant_amp[scf];
                }
            }
        }

        // Mid-side stereo
        if (ch1 != ch2) {
            float *samples_l = s->time_samples[ch1][sb];
            float *samples_r = s->time_samples[ch2][sb];
            int ch2_pres = s->ch_pres[ch2] & (1U << sb);

            for (i = 0; i < DCA_LBR_TIME_SAMPLES / 16; i++) {
                int sbms = (s->sec_ch_sbms[ch1 / 2][sb] >> i) & 1;
                int lrms = (s->sec_ch_lrms[ch1 / 2][sb] >> i) & 1;

                if (sb >= s->min_mono_subband) {
                    if (lrms && ch2_pres) {
                        if (sbms) {
                            for (j = 0; j < 16; j++) {
                                float tmp = samples_l[j];
                                samples_l[j] =  samples_r[j];
                                samples_r[j] = -tmp;
                            }
                        } else {
                            for (j = 0; j < 16; j++) {
                                float tmp = samples_l[j];
                                samples_l[j] =  samples_r[j];
                                samples_r[j] =  tmp;
                            }
                        }
                    } else if (!ch2_pres) {
                        if (sbms && (s->part_stereo_pres & (1 << ch1))) {
                            for (j = 0; j < 16; j++)
                                samples_r[j] = -samples_l[j];
                        } else {
                            for (j = 0; j < 16; j++)
                                samples_r[j] =  samples_l[j];
                        }
                    }
                } else if (sbms && ch2_pres) {
                    for (j = 0; j < 16; j++) {
                        float tmp = samples_l[j];
                        samples_l[j] = (tmp + samples_r[j]) * 0.5f;
                        samples_r[j] = (tmp - samples_r[j]) * 0.5f;
                    }
                }

                samples_l += 16;
                samples_r += 16;
            }
        }

        // Inverse prediction
        if (sb < 3)
            synth_lpc(s, ch1, ch2, sb);
    }
}

/**
 * Modulate by interpolated partial stereo coefficients
 */
static void decode_part_stereo(DCALbrDecoder *s, int ch1, int ch2)
{
    int i, ch, sb, sf;

    for (ch = ch1; ch <= ch2; ch++) {
        for (sb = s->min_mono_subband; sb < s->nsubbands; sb++) {
            uint8_t *pt_st = s->part_stereo[ch][(sb - s->min_mono_subband) / 4];
            float *samples = s->time_samples[ch][sb];

            if (s->ch_pres[ch2] & (1U << sb))
                continue;

            for (sf = 1; sf <= 4; sf++, samples += 32) {
                float prev = ff_dca_st_coeff[pt_st[sf - 1]];
                float next = ff_dca_st_coeff[pt_st[sf    ]];

                for (i = 0; i < 32; i++)
                    samples[i] *= (32 - i) * prev + i * next;
            }
        }
    }
}

/**
 * Synthesise tones in the given group for the given tonal subframe
 */
static void synth_tones(DCALbrDecoder *s, int ch, float *values,
                        int group, int group_sf, int synth_idx)
{
    int i, start, count;

    if (synth_idx < 0)
        return;

    start =  s->tonal_bounds[group][group_sf][0];
    count = (s->tonal_bounds[group][group_sf][1] - start) & (DCA_LBR_TONES - 1);

    for (i = 0; i < count; i++) {
        DCALbrTone *t = &s->tones[(start + i) & (DCA_LBR_TONES - 1)];

        if (t->amp[ch]) {
            float amp = ff_dca_synth_env[synth_idx] * ff_dca_quant_amp[t->amp[ch]];
            float c = amp * cos_tab[(t->phs[ch]     ) & 255];
            float s = amp * cos_tab[(t->phs[ch] + 64) & 255];
            const float *cf = ff_dca_corr_cf[t->f_delt];
            int x_freq = t->x_freq;

            switch (x_freq) {
            case 0:
                goto p0;
            case 1:
                values[3] += cf[0] * -s;
                values[2] += cf[1] *  c;
                values[1] += cf[2] *  s;
                values[0] += cf[3] * -c;
                goto p1;
            case 2:
                values[2] += cf[0] * -s;
                values[1] += cf[1] *  c;
                values[0] += cf[2] *  s;
                goto p2;
            case 3:
                values[1] += cf[0] * -s;
                values[0] += cf[1] *  c;
                goto p3;
            case 4:
                values[0] += cf[0] * -s;
                goto p4;
            }

            values[x_freq - 5] += cf[ 0] * -s;
        p4: values[x_freq - 4] += cf[ 1] *  c;
        p3: values[x_freq - 3] += cf[ 2] *  s;
        p2: values[x_freq - 2] += cf[ 3] * -c;
        p1: values[x_freq - 1] += cf[ 4] * -s;
        p0: values[x_freq    ] += cf[ 5] *  c;
            values[x_freq + 1] += cf[ 6] *  s;
            values[x_freq + 2] += cf[ 7] * -c;
            values[x_freq + 3] += cf[ 8] * -s;
            values[x_freq + 4] += cf[ 9] *  c;
            values[x_freq + 5] += cf[10] *  s;
        }

        t->phs[ch] += t->ph_rot;
    }
}

/**
 * Synthesise all tones in all groups for the given residual subframe
 */
static void base_func_synth(DCALbrDecoder *s, int ch, float *values, int sf)
{
    int group;

    // Tonal vs residual shift is 22 subframes
    for (group = 0; group < 5; group++) {
        int group_sf = (s->framenum << group) + ((sf - 22) >> (5 - group));
        int synth_idx = ((((sf - 22) & 31) << group) & 31) + (1 << group) - 1;

        synth_tones(s, ch, values, group, (group_sf - 1) & 31, 30 - synth_idx);
        synth_tones(s, ch, values, group, (group_sf    ) & 31,      synth_idx);
    }
}

static void transform_channel(DCALbrDecoder *s, int ch, float *output)
{
    LOCAL_ALIGNED_32(float, values, [DCA_LBR_SUBBANDS    ], [4]);
    LOCAL_ALIGNED_32(float, result, [DCA_LBR_SUBBANDS * 2], [4]);
    int sf, sb, nsubbands = s->nsubbands, noutsubbands = 8 << s->freq_range;

    // Clear inactive subbands
    if (nsubbands < noutsubbands)
        memset(values[nsubbands], 0, (noutsubbands - nsubbands) * sizeof(values[0]));

    for (sf = 0; sf < DCA_LBR_TIME_SAMPLES / 4; sf++) {
        // Hybrid filterbank
        s->dcadsp->lbr_bank(values, s->time_samples[ch],
                            ff_dca_bank_coeff, sf * 4, nsubbands);

        base_func_synth(s, ch, values[0], sf);

        s->imdct_fn(s->imdct, result[0], values[0], sizeof(float));

        // Long window and overlap-add
        s->fdsp->vector_fmul_add(output, result[0], s->window,
                                 s->history[ch], noutsubbands * 4);
        s->fdsp->vector_fmul_reverse(s->history[ch], result[noutsubbands],
                                     s->window, noutsubbands * 4);
        output += noutsubbands * 4;
    }

    // Update history for LPC and forward MDCT
    for (sb = 0; sb < nsubbands; sb++) {
        float *samples = s->time_samples[ch][sb] - DCA_LBR_TIME_HISTORY;
        memcpy(samples, samples + DCA_LBR_TIME_SAMPLES, DCA_LBR_TIME_HISTORY * sizeof(float));
    }
}

int ff_dca_lbr_filter_frame(DCALbrDecoder *s, AVFrame *frame)
{
    AVCodecContext *avctx = s->avctx;
    int i, ret, nchannels, ch_conf = (s->ch_mask & 0x7) - 1;
    const int8_t *reorder;
    uint64_t channel_mask = channel_layouts[ch_conf];

    nchannels = av_popcount64(channel_mask);
    avctx->sample_rate = s->sample_rate;
    avctx->sample_fmt = AV_SAMPLE_FMT_FLTP;
    avctx->bits_per_raw_sample = 0;
    avctx->profile = AV_PROFILE_DTS_EXPRESS;
    avctx->bit_rate = s->bit_rate_scaled;

    if (s->flags & LBR_FLAG_LFE_PRESENT) {
        channel_mask |= AV_CH_LOW_FREQUENCY;
        reorder = channel_reorder_lfe[ch_conf];
    } else {
        reorder = channel_reorder_nolfe[ch_conf];
    }

    av_channel_layout_uninit(&avctx->ch_layout);
    av_channel_layout_from_mask(&avctx->ch_layout, channel_mask);

    frame->nb_samples = 1024 << s->freq_range;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    // Filter fullband channels
    for (i = 0; i < (s->nchannels + 1) / 2; i++) {
        int ch1 = i * 2;
        int ch2 = FFMIN(ch1 + 1, s->nchannels - 1);

        decode_grid(s, ch1, ch2);

        random_ts(s, ch1, ch2);

        filter_ts(s, ch1, ch2);

        if (ch1 != ch2 && (s->part_stereo_pres & (1 << ch1)))
            decode_part_stereo(s, ch1, ch2);

        if (ch1 < nchannels)
            transform_channel(s, ch1, (float *)frame->extended_data[reorder[ch1]]);

        if (ch1 != ch2 && ch2 < nchannels)
            transform_channel(s, ch2, (float *)frame->extended_data[reorder[ch2]]);
    }

    // Interpolate LFE channel
    if (s->flags & LBR_FLAG_LFE_PRESENT) {
        s->dcadsp->lfe_iir((float *)frame->extended_data[lfe_index[ch_conf]],
                           s->lfe_data, ff_dca_lfe_iir,
                           s->lfe_history, 16 << s->freq_range);
    }

    if ((ret = ff_side_data_update_matrix_encoding(frame, AV_MATRIX_ENCODING_NONE)) < 0)
        return ret;

    return 0;
}

av_cold void ff_dca_lbr_flush(DCALbrDecoder *s)
{
    int ch, sb;

    if (!s->sample_rate)
        return;

    // Clear history
    memset(s->part_stereo, 16, sizeof(s->part_stereo));
    memset(s->lpc_coeff, 0, sizeof(s->lpc_coeff));
    memset(s->history, 0, sizeof(s->history));
    memset(s->tonal_bounds, 0, sizeof(s->tonal_bounds));
    memset(s->lfe_history, 0, sizeof(s->lfe_history));
    s->framenum = 0;
    s->ntones = 0;

    for (ch = 0; ch < s->nchannels; ch++) {
        for (sb = 0; sb < s->nsubbands; sb++) {
            float *samples = s->time_samples[ch][sb] - DCA_LBR_TIME_HISTORY;
            memset(samples, 0, DCA_LBR_TIME_HISTORY * sizeof(float));
        }
    }
}

av_cold int ff_dca_lbr_init(DCALbrDecoder *s)
{
    if (!(s->fdsp = avpriv_float_dsp_alloc(0)))
        return AVERROR(ENOMEM);

    s->lbr_rand = 1;
    return 0;
}

av_cold void ff_dca_lbr_close(DCALbrDecoder *s)
{
    s->sample_rate = 0;

    av_freep(&s->ts_buffer);
    s->ts_size = 0;

    av_freep(&s->fdsp);
    av_tx_uninit(&s->imdct);
}
