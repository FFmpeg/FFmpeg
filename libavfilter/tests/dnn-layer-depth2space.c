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
#include "libavfilter/dnn/dnn_backend_native.h"
#include "libavfilter/dnn/dnn_backend_native_layer_depth2space.h"

#define EPSON 0.00001

static int test(void)
{
    // the input data and expected data are generated with below python code.
    /*
    x = tf.placeholder(tf.float32, shape=[1, None, None, 4])
    y = tf.depth_to_space(x, 2)
    data = np.random.rand(1, 5, 3, 4);

    sess=tf.Session()
    sess.run(tf.global_variables_initializer())

    output = sess.run(y, feed_dict={x: data})

    print("input:")
    print(data.shape)
    print(list(data.flatten()))

    print("output:")
    print(output.shape)
    print(list(output.flatten()))
    */

    DepthToSpaceParams params;
    DnnOperand operands[2];
    int32_t input_indexes[1];
    float input[1*5*3*4] = {
        0.09771065121566602, 0.6336807372403175, 0.5142416549709786, 0.8027206567330333, 0.2154276025069397, 0.12112878462616772, 0.913936596765778,
        0.38881443647542646, 0.5850447615898835, 0.9311499327398275, 0.3613660929428246, 0.5420722002125493, 0.6002131190230359, 0.44800665702299525,
        0.7271322557896777, 0.3869293511885826, 0.5144404769364138, 0.6910844856987723, 0.6142102742269762, 0.6249991371621018, 0.45663376215836626,
        0.19523477129943423, 0.2483895888532045, 0.64326768256278, 0.5485877602998981, 0.45442067849873546, 0.529374943304256, 0.30439850391811885,
        0.11961343361340993, 0.2909643484561082, 0.9810970344127848, 0.8886928489786549, 0.6112237084436409, 0.8852482695156674, 0.9110868043114374,
        0.21242780027585217, 0.7101536973207572, 0.9709717457443375, 0.2702666770969332, 0.7718295953780221, 0.3957005164588574, 0.24383544252475453,
        0.040143453532367035, 0.26358051835323115, 0.013130251443791319, 0.3016550481482074, 0.03582340459943956, 0.718025513612361, 0.09844204177633753,
        0.04433767496953056, 0.6221895044119757, 0.6190414032940228, 0.8963550834625371, 0.5642449700064629, 0.2482982014723497, 0.17824909294583013,
        0.024401882408643272, 0.21742800875253465, 0.6794724473181843, 0.4814830479242237
    };
    float expected_output[1*10*6*1] = {
        0.097710654, 0.63368076, 0.2154276, 0.12112878, 0.58504474, 0.93114996, 0.51424164, 0.80272067, 0.9139366, 0.38881445,
        0.3613661, 0.5420722, 0.6002131, 0.44800666, 0.5144405, 0.6910845, 0.45663378, 0.19523478, 0.72713226, 0.38692936,
        0.61421025, 0.62499917, 0.24838959, 0.6432677, 0.54858774, 0.4544207, 0.11961343, 0.29096434, 0.6112237, 0.88524824,
        0.52937496, 0.3043985, 0.98109704, 0.88869286, 0.9110868, 0.2124278, 0.7101537, 0.97097176, 0.3957005, 0.24383545,
        0.013130251, 0.30165505, 0.27026668, 0.7718296, 0.040143453, 0.26358053, 0.035823405, 0.7180255, 0.09844204,
        0.044337675, 0.8963551, 0.564245, 0.024401883, 0.21742801, 0.6221895, 0.6190414, 0.2482982, 0.17824909, 0.67947245, 0.48148304
    };
    float *output;

    operands[0].data = input;
    operands[0].dims[0] = 1;
    operands[0].dims[1] = 5;
    operands[0].dims[2] = 3;
    operands[0].dims[3] = 4;
    operands[1].data = NULL;

    input_indexes[0] = 0;
    params.block_size = 2;
    ff_dnn_execute_layer_depth2space(operands, input_indexes, 1, &params, NULL);

    output = operands[1].data;
    for (int i = 0; i < sizeof(expected_output) / sizeof(float); i++) {
        if (fabs(output[i] - expected_output[i]) > EPSON) {
            printf("at index %d, output: %f, expected_output: %f\n", i, output[i], expected_output[i]);
            av_freep(&output);
            return 1;
        }
    }

    av_freep(&output);
    return 0;
}

int main(int argc, char **argv)
{
    return test();
}
