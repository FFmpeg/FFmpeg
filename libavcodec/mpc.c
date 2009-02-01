/*
 * Musepack decoder core
 * Copyright (c) 2006 Konstantin Shishkov
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
 * @file libavcodec/mpc.c Musepack decoder core
 * MPEG Audio Layer 1/2 -like codec with frames of 1152 samples
 * divided into 32 subbands.
 */

#include "libavutil/random.h"
#include "avcodec.h"
#include "bitstream.h"
#include "dsputil.h"
#include "mpegaudio.h"

#include "mpc.h"
#include "mpcdata.h"

static DECLARE_ALIGNED_16(MPA_INT, mpa_window[512]);

void ff_mpc_init(void)
{
    ff_mpa_synth_init(mpa_window);
}

/**
 * Process decoded Musepack data and produce PCM
 */
static void mpc_synth(MPCContext *c, int16_t *out)
{
    int dither_state = 0;
    int i, ch;
    OUT_INT samples[MPA_MAX_CHANNELS * MPA_FRAME_SIZE], *samples_ptr;

    for(ch = 0;  ch < 2; ch++){
        samples_ptr = samples + ch;
        for(i = 0; i < SAMPLES_PER_BAND; i++) {
            ff_mpa_synth_filter(c->synth_buf[ch], &(c->synth_buf_offset[ch]),
                                mpa_window, &dither_state,
                                samples_ptr, 2,
                                c->sb_samples[ch][i]);
            samples_ptr += 64;
        }
    }
    for(i = 0; i < MPC_FRAME_SIZE*2; i++)
        *out++=samples[i];
}

void ff_mpc_dequantize_and_synth(MPCContext * c, int maxband, void *data)
{
    int i, j, ch;
    Band *bands = c->bands;
    int off;
    float mul;

    /* dequantize */
    memset(c->sb_samples, 0, sizeof(c->sb_samples));
    off = 0;
    for(i = 0; i <= maxband; i++, off += SAMPLES_PER_BAND){
        for(ch = 0; ch < 2; ch++){
            if(bands[i].res[ch]){
                j = 0;
                mul = mpc_CC[bands[i].res[ch]] * mpc_SCF[bands[i].scf_idx[ch][0]];
                for(; j < 12; j++)
                    c->sb_samples[ch][j][i] = mul * c->Q[ch][j + off];
                mul = mpc_CC[bands[i].res[ch]] * mpc_SCF[bands[i].scf_idx[ch][1]];
                for(; j < 24; j++)
                    c->sb_samples[ch][j][i] = mul * c->Q[ch][j + off];
                mul = mpc_CC[bands[i].res[ch]] * mpc_SCF[bands[i].scf_idx[ch][2]];
                for(; j < 36; j++)
                    c->sb_samples[ch][j][i] = mul * c->Q[ch][j + off];
            }
        }
        if(bands[i].msf){
            int t1, t2;
            for(j = 0; j < SAMPLES_PER_BAND; j++){
                t1 = c->sb_samples[0][j][i];
                t2 = c->sb_samples[1][j][i];
                c->sb_samples[0][j][i] = t1 + t2;
                c->sb_samples[1][j][i] = t1 - t2;
            }
        }
    }

    mpc_synth(c, data);
}
