/*
 * linear least squares model
 *
 * Copyright (c) 2006 Michael Niedermayer <michaelni@gmx.at>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * linear least squares model
 */

#include <math.h>
#include <string.h>

#include "lls.h"

void av_init_lls(LLSModel *m, int indep_count)
{
    memset(m, 0, sizeof(LLSModel));
    m->indep_count = indep_count;
}

void av_update_lls(LLSModel *m, double *var, double decay)
{
    int i, j;

    for (i = 0; i <= m->indep_count; i++) {
        for (j = i; j <= m->indep_count; j++) {
            m->covariance[i][j] *= decay;
            m->covariance[i][j] += var[i] * var[j];
        }
    }
}

void av_solve_lls(LLSModel *m, double threshold, int min_order)
{
    int i, j, k;
    double (*factor)[MAX_VARS + 1] = (void *) &m->covariance[1][0];
    double (*covar) [MAX_VARS + 1] = (void *) &m->covariance[1][1];
    double *covar_y                = m->covariance[0];
    int count                      = m->indep_count;

    for (i = 0; i < count; i++) {
        for (j = i; j < count; j++) {
            double sum = covar[i][j];

            for (k = i - 1; k >= 0; k--)
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

        for (k = i - 1; k >= 0; k--)
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

double av_evaluate_lls(LLSModel *m, double *param, int order)
{
    int i;
    double out = 0;

    for (i = 0; i <= order; i++)
        out += param[i] * m->coeff[order][i];

    return out;
}

#ifdef TEST

#include <stdio.h>
#include <limits.h>
#include "lfg.h"

int main(void)
{
    LLSModel m;
    int i, order;
    AVLFG lfg;

    av_lfg_init(&lfg, 1);
    av_init_lls(&m, 3);

    for (i = 0; i < 100; i++) {
        double var[4];
        double eval;

        var[0] = (av_lfg_get(&lfg) / (double) UINT_MAX - 0.5) * 2;
        var[1] = var[0] + av_lfg_get(&lfg) / (double) UINT_MAX - 0.5;
        var[2] = var[1] + av_lfg_get(&lfg) / (double) UINT_MAX - 0.5;
        var[3] = var[2] + av_lfg_get(&lfg) / (double) UINT_MAX - 0.5;
        av_update_lls(&m, var, 0.99);
        av_solve_lls(&m, 0.001, 0);
        for (order = 0; order < 3; order++) {
            eval = av_evaluate_lls(&m, var + 1, order);
            printf("real:%9f order:%d pred:%9f var:%f coeffs:%f %9f %9f\n",
                   var[0], order, eval, sqrt(m.variance[order] / (i + 1)),
                   m.coeff[order][0], m.coeff[order][1],
                   m.coeff[order][2]);
        }
    }
    return 0;
}

#endif
