/*
 * Copyright (c) 2020
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

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "libavfilter/dnn/dnn_backend_native_layer_mathbinary.h"
#include "libavutil/avassert.h"

#define EPSON 0.00005

static float get_expected(float f1, float f2, DNNMathBinaryOperation op)
{
    switch (op)
    {
    case DMBO_SUB:
        return f1 - f2;
    case DMBO_ADD:
        return f1 + f2;
    case DMBO_MUL:
        return f1 * f2;
    case DMBO_REALDIV:
        return f1 / f2;
    case DMBO_MINIMUM:
        return (f1 < f2) ? f1 : f2;
    case DMBO_FLOORMOD:
        return (float)((int)(f1) % (int)(f2));
    default:
        av_assert0(!"not supported yet");
        return 0.f;
    }
}

static int test_broadcast_input0(DNNMathBinaryOperation op)
{
    DnnLayerMathBinaryParams params;
    DnnOperand operands[2];
    int32_t input_indexes[1];
    float input[1*1*2*3] = {
        -3, 2.5, 2, -2.1, 7.8, 100
    };
    float *output;

    params.bin_op = op;
    params.input0_broadcast = 1;
    params.input1_broadcast = 0;
    params.v = 7.28;

    operands[0].data = input;
    operands[0].dims[0] = 1;
    operands[0].dims[1] = 1;
    operands[0].dims[2] = 2;
    operands[0].dims[3] = 3;
    operands[1].data = NULL;

    input_indexes[0] = 0;
    ff_dnn_execute_layer_math_binary(operands, input_indexes, 1, &params, NULL);

    output = operands[1].data;
    for (int i = 0; i < sizeof(input) / sizeof(float); i++) {
        float expected_output = get_expected(params.v, input[i], op);
        if (fabs(output[i] - expected_output) > EPSON) {
            printf("op %d, at index %d, output: %f, expected_output: %f (%s:%d)\n",
                    op, i, output[i], expected_output, __FILE__, __LINE__);
            av_freep(&output);
            return 1;
        }
    }

    av_freep(&output);
    return 0;
}

static int test_broadcast_input1(DNNMathBinaryOperation op)
{
    DnnLayerMathBinaryParams params;
    DnnOperand operands[2];
    int32_t input_indexes[1];
    float input[1*1*2*3] = {
        -3, 2.5, 2, -2.1, 7.8, 100
    };
    float *output;

    params.bin_op = op;
    params.input0_broadcast = 0;
    params.input1_broadcast = 1;
    params.v = 7.28;

    operands[0].data = input;
    operands[0].dims[0] = 1;
    operands[0].dims[1] = 1;
    operands[0].dims[2] = 2;
    operands[0].dims[3] = 3;
    operands[1].data = NULL;

    input_indexes[0] = 0;
    ff_dnn_execute_layer_math_binary(operands, input_indexes, 1, &params, NULL);

    output = operands[1].data;
    for (int i = 0; i < sizeof(input) / sizeof(float); i++) {
        float expected_output = get_expected(input[i], params.v, op);
        if (fabs(output[i] - expected_output) > EPSON) {
            printf("op %d, at index %d, output: %f, expected_output: %f (%s:%d)\n",
                    op, i, output[i], expected_output, __FILE__, __LINE__);
            av_freep(&output);
            return 1;
        }
    }

    av_freep(&output);
    return 0;
}

static int test_no_broadcast(DNNMathBinaryOperation op)
{
    DnnLayerMathBinaryParams params;
    DnnOperand operands[3];
    int32_t input_indexes[2];
    float input0[1*1*2*3] = {
        -3, 2.5, 2, -2.1, 7.8, 100
    };
    float input1[1*1*2*3] = {
        -1, 2, 3, -21, 8, 10.0
    };
    float *output;

    params.bin_op = op;
    params.input0_broadcast = 0;
    params.input1_broadcast = 0;

    operands[0].data = input0;
    operands[0].dims[0] = 1;
    operands[0].dims[1] = 1;
    operands[0].dims[2] = 2;
    operands[0].dims[3] = 3;
    operands[1].data = input1;
    operands[1].dims[0] = 1;
    operands[1].dims[1] = 1;
    operands[1].dims[2] = 2;
    operands[1].dims[3] = 3;
    operands[2].data = NULL;

    input_indexes[0] = 0;
    input_indexes[1] = 1;
    ff_dnn_execute_layer_math_binary(operands, input_indexes, 2, &params, NULL);

    output = operands[2].data;
    for (int i = 0; i < sizeof(input0) / sizeof(float); i++) {
        float expected_output = get_expected(input0[i], input1[i], op);
        if (fabs(output[i] - expected_output) > EPSON) {
            printf("op %d, at index %d, output: %f, expected_output: %f (%s:%d)\n",
                    op, i, output[i], expected_output, __FILE__, __LINE__);
            av_freep(&output);
            return 1;
        }
    }

    av_freep(&output);
    return 0;
}

static int test(DNNMathBinaryOperation op)
{
    if (test_broadcast_input0(op))
        return 1;

    if (test_broadcast_input1(op))
        return 1;

    if (test_no_broadcast(op))
        return 1;

    return 0;
}

int main(int argc, char **argv)
{
    if (test(DMBO_SUB))
        return 1;

    if (test(DMBO_ADD))
        return 1;

    if (test(DMBO_MUL))
        return 1;

    if (test(DMBO_REALDIV))
        return 1;

    if (test(DMBO_MINIMUM))
        return 1;

    if (test(DMBO_FLOORMOD))
        return 1;

    return 0;
}
