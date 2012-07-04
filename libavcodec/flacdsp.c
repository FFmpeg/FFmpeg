/*
 * Copyright (c) 2012 Mans Rullgard <mans@mansr.com>
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

#include "libavutil/attributes.h"
#include "libavutil/samplefmt.h"
#include "flacdsp.h"

#define SAMPLE_SIZE 16
#include "flacdsp_template.c"

#undef  SAMPLE_SIZE
#define SAMPLE_SIZE 32
#include "flacdsp_template.c"

static void flac_lpc_16_c(int32_t *decoded, const int coeffs[32],
                          int pred_order, int qlevel, int len)
{
    int i, j;

    for (i = pred_order; i < len - 1; i += 2) {
        int c;
        int d = decoded[i-pred_order];
        int s0 = 0, s1 = 0;
        for (j = pred_order-1; j > 0; j--) {
            c = coeffs[j];
            s0 += c*d;
            d = decoded[i-j];
            s1 += c*d;
        }
        c = coeffs[0];
        s0 += c*d;
        d = decoded[i] += s0 >> qlevel;
        s1 += c*d;
        decoded[i+1] += s1 >> qlevel;
    }
    if (i < len) {
        int sum = 0;
        for (j = 0; j < pred_order; j++)
            sum += coeffs[j] * decoded[i-j-1];
        decoded[i] += sum >> qlevel;
    }
}

static void flac_lpc_32_c(int32_t *decoded, const int coeffs[32],
                          int pred_order, int qlevel, int len)
{
    int i, j;

    for (i = pred_order; i < len; i++) {
        int64_t sum = 0;
        for (j = 0; j < pred_order; j++)
            sum += (int64_t)coeffs[j] * decoded[i-j-1];
        decoded[i] += sum >> qlevel;
    }

}

av_cold void ff_flacdsp_init(FLACDSPContext *c, enum AVSampleFormat fmt)
{
    switch (fmt) {
    case AV_SAMPLE_FMT_S32:
        c->decorrelate[0] = flac_decorrelate_indep_c_32;
        c->decorrelate[1] = flac_decorrelate_ls_c_32;
        c->decorrelate[2] = flac_decorrelate_rs_c_32;
        c->decorrelate[3] = flac_decorrelate_ms_c_32;
        c->lpc            = flac_lpc_32_c;
        break;

    case AV_SAMPLE_FMT_S16:
        c->decorrelate[0] = flac_decorrelate_indep_c_16;
        c->decorrelate[1] = flac_decorrelate_ls_c_16;
        c->decorrelate[2] = flac_decorrelate_rs_c_16;
        c->decorrelate[3] = flac_decorrelate_ms_c_16;
        c->lpc            = flac_lpc_16_c;
        break;
    }
}
