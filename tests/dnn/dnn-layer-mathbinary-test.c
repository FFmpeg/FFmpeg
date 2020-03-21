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

#define EPSON 0.00001

static int test_sub_broadcast_input0(void)
{
    DnnLayerMathBinaryParams params;
    DnnOperand operands[2];
    int32_t input_indexes[1];
    float input[1*1*2*3] = {
        -3, 2.5, 2, -2.1, 7.8, 100
    };
    float *output;

    params.bin_op = DMBO_SUB;
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
    dnn_execute_layer_math_binary(operands, input_indexes, 1, &params);

    output = operands[1].data;
    for (int i = 0; i < sizeof(input) / sizeof(float); i++) {
        float expected_output = params.v - input[i];
        if (fabs(output[i] - expected_output) > EPSON) {
            printf("at index %d, output: %f, expected_output: %f\n", i, output[i], expected_output);
            av_freep(&output);
            return 1;
        }
    }

    av_freep(&output);
    return 0;
}

static int test_sub_broadcast_input1(void)
{
    DnnLayerMathBinaryParams params;
    DnnOperand operands[2];
    int32_t input_indexes[1];
    float input[1*1*2*3] = {
        -3, 2.5, 2, -2.1, 7.8, 100
    };
    float *output;

    params.bin_op = DMBO_SUB;
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
    dnn_execute_layer_math_binary(operands, input_indexes, 1, &params);

    output = operands[1].data;
    for (int i = 0; i < sizeof(input) / sizeof(float); i++) {
        float expected_output = input[i] - params.v;
        if (fabs(output[i] - expected_output) > EPSON) {
            printf("at index %d, output: %f, expected_output: %f\n", i, output[i], expected_output);
            av_freep(&output);
            return 1;
        }
    }

    av_freep(&output);
    return 0;
}

static int test_sub_no_broadcast(void)
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

    params.bin_op = DMBO_SUB;
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
    dnn_execute_layer_math_binary(operands, input_indexes, 2, &params);

    output = operands[2].data;
    for (int i = 0; i < sizeof(input0) / sizeof(float); i++) {
        float expected_output = input0[i] - input1[i];
        if (fabs(output[i] - expected_output) > EPSON) {
            printf("at index %d, output: %f, expected_output: %f\n", i, output[i], expected_output);
            av_freep(&output);
            return 1;
        }
    }

    av_freep(&output);
    return 0;
}

static int test_sub(void)
{
    if (test_sub_broadcast_input0())
        return 1;

    if (test_sub_broadcast_input1())
        return 1;

    if (test_sub_no_broadcast())
        return 1;

    return 0;
}

int main(int argc, char **argv)
{
    if (test_sub())
        return 1;

    return 0;
}
