/*
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

#include <limits.h>
#include <stdio.h>

#include "libavutil/internal.h"
#include "libavutil/lfg.h"
#include "libavutil/lls.h"

int main(void)
{
    LLSModel m;
    int i, order;
    AVLFG lfg;

    av_lfg_init(&lfg, 1);
    avpriv_init_lls(&m, 3);

    for (i = 0; i < 100; i++) {
        LOCAL_ALIGNED(32, double, var, [4]);
        double eval;

        var[0] =         (av_lfg_get(&lfg) / (double) UINT_MAX - 0.5) * 2;
        var[1] = var[0] + av_lfg_get(&lfg) / (double) UINT_MAX - 0.5;
        var[2] = var[1] + av_lfg_get(&lfg) / (double) UINT_MAX - 0.5;
        var[3] = var[2] + av_lfg_get(&lfg) / (double) UINT_MAX - 0.5;
        m.update_lls(&m, var);
        avpriv_solve_lls(&m, 0.001, 0);
        for (order = 0; order < 3; order++) {
            eval = m.evaluate_lls(&m, var + 1, order);
            printf("real:%9f order:%d pred:%9f var:%f coeffs:%f %9f %9f\n",
                   var[0], order, eval, sqrt(m.variance[order] / (i + 1)),
                   m.coeff[order][0], m.coeff[order][1],
                   m.coeff[order][2]);
        }
    }
    return 0;
}
