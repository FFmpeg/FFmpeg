/*
 * Copyright (c) 2019 Guo Yejun
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
#include "libavfilter/dnn/dnn_backend_native_layer_pad.h"

#define EPSON 0.00001

static int test_with_mode_symmetric(void)
{
    // the input data and expected data are generated with below python code.
    /*
    x = tf.placeholder(tf.float32, shape=[1, None, None, 3])
    y = tf.pad(x, [[0, 0], [2, 3], [3, 2], [0, 0]], 'SYMMETRIC')
    data = np.arange(48).reshape(1, 4, 4, 3);

    sess=tf.Session()
    sess.run(tf.global_variables_initializer())
    output = sess.run(y, feed_dict={x: data})

    print(list(data.flatten()))
    print(list(output.flatten()))
    print(data.shape)
    print(output.shape)
    */

    LayerPadParams params;
    float input[1*4*4*3] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47
    };
    float expected_output[1*9*9*3] = {
        18.0, 19.0, 20.0, 15.0, 16.0, 17.0, 12.0, 13.0, 14.0, 12.0, 13.0, 14.0, 15.0, 16.0, 17.0, 18.0, 19.0, 20.0, 21.0, 22.0, 23.0, 21.0, 22.0, 23.0, 18.0, 19.0, 20.0, 6.0, 7.0, 8.0, 3.0,
        4.0, 5.0, 0.0, 1.0, 2.0, 0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0, 11.0, 9.0, 10.0, 11.0, 6.0, 7.0, 8.0, 6.0, 7.0, 8.0, 3.0, 4.0, 5.0, 0.0, 1.0, 2.0, 0.0, 1.0, 2.0, 3.0,
        4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0, 11.0, 9.0, 10.0, 11.0, 6.0, 7.0, 8.0, 18.0, 19.0, 20.0, 15.0, 16.0, 17.0, 12.0, 13.0, 14.0, 12.0, 13.0, 14.0, 15.0, 16.0, 17.0, 18.0, 19.0, 20.0,
        21.0, 22.0, 23.0, 21.0, 22.0, 23.0, 18.0, 19.0, 20.0, 30.0, 31.0, 32.0, 27.0, 28.0, 29.0, 24.0, 25.0, 26.0, 24.0, 25.0, 26.0, 27.0, 28.0, 29.0, 30.0, 31.0, 32.0, 33.0, 34.0, 35.0, 33.0,
        34.0, 35.0, 30.0, 31.0, 32.0, 42.0, 43.0, 44.0, 39.0, 40.0, 41.0, 36.0, 37.0, 38.0, 36.0, 37.0, 38.0, 39.0, 40.0, 41.0, 42.0, 43.0, 44.0, 45.0, 46.0, 47.0, 45.0, 46.0, 47.0, 42.0, 43.0,
        44.0, 42.0, 43.0, 44.0, 39.0, 40.0, 41.0, 36.0, 37.0, 38.0, 36.0, 37.0, 38.0, 39.0, 40.0, 41.0, 42.0, 43.0, 44.0, 45.0, 46.0, 47.0, 45.0, 46.0, 47.0, 42.0, 43.0, 44.0, 30.0, 31.0, 32.0,
        27.0, 28.0, 29.0, 24.0, 25.0, 26.0, 24.0, 25.0, 26.0, 27.0, 28.0, 29.0, 30.0, 31.0, 32.0, 33.0, 34.0, 35.0, 33.0, 34.0, 35.0, 30.0, 31.0, 32.0, 18.0, 19.0, 20.0, 15.0, 16.0, 17.0, 12.0,
        13.0, 14.0, 12.0, 13.0, 14.0, 15.0, 16.0, 17.0, 18.0, 19.0, 20.0, 21.0, 22.0, 23.0, 21.0, 22.0, 23.0, 18.0, 19.0, 20.0
    };
    float output[1*9*9*3];
    memset(output, 0, sizeof(output));

    params.mode = LPMP_SYMMETRIC;
    params.paddings[0][0] = 0;
    params.paddings[0][1] = 0;
    params.paddings[1][0] = 2;
    params.paddings[1][1] = 3;
    params.paddings[2][0] = 3;
    params.paddings[2][1] = 2;
    params.paddings[3][0] = 0;
    params.paddings[3][1] = 0;

    dnn_execute_layer_pad(input, output, &params, 1, 4, 4, 3);

    for (int i = 0; i < sizeof(output) / sizeof(float); i++) {
        if (fabs(output[i] - expected_output[i]) > EPSON) {
            printf("at index %d, output: %f, expected_output: %f\n", i, output[i], expected_output[i]);
            return 1;
        }
    }

    return 0;

}

static int test_with_mode_reflect(void)
{
    // the input data and expected data are generated with below python code.
    /*
    x = tf.placeholder(tf.float32, shape=[3, None, None, 3])
    y = tf.pad(x, [[1, 2], [0, 0], [0, 0], [0, 0]], 'REFLECT')
    data = np.arange(36).reshape(3, 2, 2, 3);

    sess=tf.Session()
    sess.run(tf.global_variables_initializer())
    output = sess.run(y, feed_dict={x: data})

    print(list(data.flatten()))
    print(list(output.flatten()))
    print(data.shape)
    print(output.shape)
    */

    LayerPadParams params;
    float input[3*2*2*3] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35
    };
    float expected_output[6*2*2*3] = {
        12.0, 13.0, 14.0, 15.0, 16.0, 17.0, 18.0, 19.0, 20.0, 21.0, 22.0, 23.0, 0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0, 11.0,
        12.0, 13.0, 14.0, 15.0, 16.0, 17.0, 18.0, 19.0, 20.0, 21.0, 22.0, 23.0, 24.0, 25.0, 26.0, 27.0, 28.0, 29.0, 30.0, 31.0, 32.0, 33.0, 34.0,
        35.0, 12.0, 13.0, 14.0, 15.0, 16.0, 17.0, 18.0, 19.0, 20.0, 21.0, 22.0, 23.0, 0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0, 11.0
    };
    float output[6*2*2*3];
    memset(output, 0, sizeof(output));

    params.mode = LPMP_REFLECT;
    params.paddings[0][0] = 1;
    params.paddings[0][1] = 2;
    params.paddings[1][0] = 0;
    params.paddings[1][1] = 0;
    params.paddings[2][0] = 0;
    params.paddings[2][1] = 0;
    params.paddings[3][0] = 0;
    params.paddings[3][1] = 0;

    dnn_execute_layer_pad(input, output, &params, 3, 2, 2, 3);

    for (int i = 0; i < sizeof(output) / sizeof(float); i++) {
        if (fabs(output[i] - expected_output[i]) > EPSON) {
            printf("at index %d, output: %f, expected_output: %f\n", i, output[i], expected_output[i]);
            return 1;
        }
    }

    return 0;

}

static int test_with_mode_constant(void)
{
    // the input data and expected data are generated with below python code.
    /*
    x = tf.placeholder(tf.float32, shape=[1, None, None, 3])
    y = tf.pad(x, [[0, 0], [1, 0], [0, 0], [1, 2]], 'CONSTANT', constant_values=728)
    data = np.arange(12).reshape(1, 2, 2, 3);

    sess=tf.Session()
    sess.run(tf.global_variables_initializer())
    output = sess.run(y, feed_dict={x: data})

    print(list(data.flatten()))
    print(list(output.flatten()))
    print(data.shape)
    print(output.shape)
    */

    LayerPadParams params;
    float input[1*2*2*3] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
    };
    float expected_output[1*3*2*6] = {
        728.0, 728.0, 728.0, 728.0, 728.0, 728.0, 728.0, 728.0, 728.0, 728.0, 728.0,
        728.0, 728.0, 0.0, 1.0, 2.0, 728.0, 728.0, 728.0, 3.0, 4.0, 5.0, 728.0, 728.0,
        728.0, 6.0, 7.0, 8.0, 728.0, 728.0, 728.0, 9.0, 10.0, 11.0, 728.0, 728.0
    };
    float output[1*3*2*6];
    memset(output, 0, sizeof(output));

    params.mode = LPMP_CONSTANT;
    params.constant_values = 728;
    params.paddings[0][0] = 0;
    params.paddings[0][1] = 0;
    params.paddings[1][0] = 1;
    params.paddings[1][1] = 0;
    params.paddings[2][0] = 0;
    params.paddings[2][1] = 0;
    params.paddings[3][0] = 1;
    params.paddings[3][1] = 2;

    dnn_execute_layer_pad(input, output, &params, 1, 2, 2, 3);

    for (int i = 0; i < sizeof(output) / sizeof(float); i++) {
        if (fabs(output[i] - expected_output[i]) > EPSON) {
            printf("at index %d, output: %f, expected_output: %f\n", i, output[i], expected_output[i]);
            return 1;
        }
    }

    return 0;

}

int main(int argc, char **argv)
{
    if (test_with_mode_symmetric())
        return 1;

    if (test_with_mode_reflect())
        return 1;

    if (test_with_mode_constant())
        return 1;
}
