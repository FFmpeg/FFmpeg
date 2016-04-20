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

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "libavutil/libm.h"
#include "libavutil/timer.h"
#include "libavutil/eval.h"

static const double const_values[] = {
    M_PI,
    M_E,
    0
};

static const char *const const_names[] = {
    "PI",
    "E",
    0
};

int main(int argc, char **argv)
{
    int i;
    double d;
    const char *const *expr;
    static const char *const exprs[] = {
        "",
        "1;2",
        "-20",
        "-PI",
        "+PI",
        "1+(5-2)^(3-1)+1/2+sin(PI)-max(-2.2,-3.1)",
        "80G/80Gi",
        "1k",
        "1Gi",
        "1gi",
        "1GiFoo",
        "1k+1k",
        "1Gi*3foo",
        "foo",
        "foo(",
        "foo()",
        "foo)",
        "sin",
        "sin(",
        "sin()",
        "sin)",
        "sin 10",
        "sin(1,2,3)",
        "sin(1 )",
        "1",
        "1foo",
        "bar + PI + E + 100f*2 + foo",
        "13k + 12f - foo(1, 2)",
        "1gi",
        "1Gi",
        "st(0, 123)",
        "st(1, 123); ld(1)",
        "lte(0, 1)",
        "lte(1, 1)",
        "lte(1, 0)",
        "lt(0, 1)",
        "lt(1, 1)",
        "gt(1, 0)",
        "gt(2, 7)",
        "gte(122, 122)",
        /* compute 1+2+...+N */
        "st(0, 1); while(lte(ld(0), 100), st(1, ld(1)+ld(0));st(0, ld(0)+1)); ld(1)",
        /* compute Fib(N) */
        "st(1, 1); st(2, 2); st(0, 1); while(lte(ld(0),10), st(3, ld(1)+ld(2)); st(1, ld(2)); st(2, ld(3)); st(0, ld(0)+1)); ld(3)",
        "while(0, 10)",
        "st(0, 1); while(lte(ld(0),100), st(1, ld(1)+ld(0)); st(0, ld(0)+1))",
        "isnan(1)",
        "isnan(NAN)",
        "isnan(INF)",
        "isinf(1)",
        "isinf(NAN)",
        "isinf(INF)",
        "floor(NAN)",
        "floor(123.123)",
        "floor(-123.123)",
        "trunc(123.123)",
        "trunc(-123.123)",
        "ceil(123.123)",
        "ceil(-123.123)",
        "sqrt(1764)",
        "isnan(sqrt(-1))",
        "not(1)",
        "not(NAN)",
        "not(0)",
        "6.0206dB",
        "-3.0103dB",
        NULL
    };

    for (expr = exprs; *expr; expr++) {
        printf("Evaluating '%s'\n", *expr);
        av_expr_parse_and_eval(&d, *expr,
                               const_names, const_values,
                               NULL, NULL, NULL, NULL, NULL, 0, NULL);
        if (isnan(d))
            printf("'%s' -> nan\n\n", *expr);
        else
            printf("'%s' -> %f\n\n", *expr, d);
    }

    av_expr_parse_and_eval(&d, "1+(5-2)^(3-1)+1/2+sin(PI)-max(-2.2,-3.1)",
                           const_names, const_values,
                           NULL, NULL, NULL, NULL, NULL, 0, NULL);
    printf("%f == 12.7\n", d);
    av_expr_parse_and_eval(&d, "80G/80Gi",
                           const_names, const_values,
                           NULL, NULL, NULL, NULL, NULL, 0, NULL);
    printf("%f == 0.931322575\n", d);

    if (argc > 1 && !strcmp(argv[1], "-t")) {
        for (i = 0; i < 1050; i++) {
            START_TIMER;
            av_expr_parse_and_eval(&d, "1+(5-2)^(3-1)+1/2+sin(PI)-max(-2.2,-3.1)",
                                   const_names, const_values,
                                   NULL, NULL, NULL, NULL, NULL, 0, NULL);
            STOP_TIMER("av_expr_parse_and_eval");
        }
    }

    return 0;
}
