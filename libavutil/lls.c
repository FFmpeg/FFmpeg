/*
 * linear least squares model
 *
 * Copyright (c) 2006 Michael Niedermayer <michaelni@gmx.at>
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
 * linear least squares model
 */

#include <math.h>
#include <string.h>

#include "config.h"
#include "attributes.h"
#include "lls.h"

static void update_lls(LLSModel *m, const double *var)
{
    int i, j;

    for (i = 0; i <= m->indep_count; i++) {
        for (j = i; j <= m->indep_count; j++) {
            m->covariance[i][j] += var[i] * var[j];
        }
    }
}

void avpriv_solve_lls(LLSModel *m, double threshold, unsigned short min_order)
{
    int i, j, k;
    double (*factor)[MAX_VARS_ALIGN] = (void *) &m->covariance[1][0];
    double (*covar) [MAX_VARS_ALIGN] = (void *) &m->covariance[1][1];
    double *covar_y                = m->covariance[0];
    int count                      = m->indep_count;

    for (i = 0; i < count; i++) {
        for (j = i; j < count; j++) {
            double sum = covar[i][j];

            for (k = 0; k <= i-1; k++)
                sum -= factor[i][k] * factor[j][k];

            if (i == j) {
                if (sum < threshold)
                    sum = 1.0;
                factor[i][i] = sqrt(sum);
            } else {
                factor[j][i] = sum / factor[i][i];
            }
        }
    }

    for (i = 0; i < count; i++) {
        double sum = covar_y[i + 1];

        for (k = 0; k <= i-1; k++)
            sum -= factor[i][k] * m->coeff[0][k];

        m->coeff[0][i] = sum / factor[i][i];
    }

    for (j = count - 1; j >= min_order; j--) {
        for (i = j; i >= 0; i--) {
            double sum = m->coeff[0][i];

            for (k = i + 1; k <= j; k++)
                sum -= factor[k][i] * m->coeff[j][k];

            m->coeff[j][i] = sum / factor[i][i];
        }

        m->variance[j] = covar_y[0];

        for (i = 0; i <= j; i++) {
            double sum = m->coeff[j][i] * covar[i][i] - 2 * covar_y[i + 1];

            for (k = 0; k < i; k++)
                sum += 2 * m->coeff[j][k] * covar[k][i];

            m->variance[j] += m->coeff[j][i] * sum;
        }
    }
}

static double evaluate_lls(LLSModel *m, const double *param, int order)
{
    int i;
    double out = 0;

    for (i = 0; i <= order; i++)
        out += param[i] * m->coeff[order][i];

    return out;
}

av_cold void avpriv_init_lls(LLSModel *m, int indep_count)
{
    memset(m, 0, sizeof(LLSModel));
    m->indep_count = indep_count;
    m->update_lls = update_lls;
    m->evaluate_lls = evaluate_lls;
    if (ARCH_X86)
        ff_init_lls_x86(m);
}
