/*
 * Copyright © 2024 Rémi Denis-Courmont.
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

#include <assert.h>
#include <stdint.h>

#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/riscv/cpu.h"
#include "libavutil/lls.h"

void ff_lls_update_covariance_rvv(double covar[][36], const double *var,
                                  int count);
double ff_scalarproduct_double_rvv(const double *, const double *, size_t);

static void ff_lls_update_rvv(struct LLSModel *m, const double *var)
{
    ff_lls_update_covariance_rvv(m->covariance, var, m->indep_count + 1);
}

static double ff_lls_evaluate_rvv(struct LLSModel *m, const double *var,
                                  int order)
{
    return ff_scalarproduct_double_rvv(m->coeff[order], var, order + 1);
}

av_cold void ff_init_lls_riscv(LLSModel *m)
{
#if HAVE_RVV
    int flags = av_get_cpu_flags();

    if ((flags & AV_CPU_FLAG_RVB) && (flags & AV_CPU_FLAG_RVV_F64)) {
        if (ff_get_rv_vlenb() > m->indep_count)
            m->update_lls = ff_lls_update_rvv;
        m->evaluate_lls = ff_lls_evaluate_rvv;
    }
#endif
}
