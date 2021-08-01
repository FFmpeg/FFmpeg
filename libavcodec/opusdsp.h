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

#ifndef AVCODEC_OPUSDSP_H
#define AVCODEC_OPUSDSP_H

#define CELT_EMPH_COEFF 0.8500061035f

typedef struct OpusDSP {
    void (*postfilter)(float *data, int period, float *gains, int len);
    float (*deemphasis)(float *out, float *in, float coeff, int len);
} OpusDSP;

void ff_opus_dsp_init(OpusDSP *ctx);

void ff_opus_dsp_init_x86(OpusDSP *ctx);
void ff_opus_dsp_init_aarch64(OpusDSP *ctx);

#endif /* AVCODEC_OPUSDSP_H */
