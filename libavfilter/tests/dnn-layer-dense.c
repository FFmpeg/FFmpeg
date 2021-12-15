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
#include "libavfilter/dnn/dnn_backend_native_layer_dense.h"

#define EPSON 0.00001

static int test(void)
{
    // the input data and expected data are generated with below python code.
    /*
    x = tf.placeholder(tf.float32, shape=[1, None, None, 3])
    y = tf.layers.dense(input_x, 3, activation=tf.nn.sigmoid, bias_initializer=tf.keras.initializers.he_normal())
    data = np.random.rand(1, 5, 6, 3);

    sess=tf.Session()
    sess.run(tf.global_variables_initializer())

    weights = dict([(var.name, sess.run(var)) for var in tf.trainable_variables()])
    kernel = weights['dense/kernel:0']
    kernel = np.transpose(kernel, [1, 0])
    print("kernel:")
    print(kernel.shape)
    print(list(kernel.flatten()))

    bias = weights['dense/bias:0']
    print("bias:")
    print(bias.shape)
    print(list(bias.flatten()))

    output = sess.run(y, feed_dict={x: data})

    print("input:")
    print(data.shape)
    print(list(data.flatten()))

    print("output:")
    print(output.shape)
    print(list(output.flatten()))
    */

    DenseParams params;
    DnnOperand operands[2];
    int32_t input_indexes[1];
    float input[1*5*6*3] = {
        0.5552418686576308, 0.20653189262022464, 0.31115120939398877, 0.5897014433221428, 0.37340078861060655, 0.6470921693941893, 0.8039950367872679, 0.8762700891949274,
        0.6556655583829558, 0.5911096107039339, 0.18640250865290997, 0.2803248779238966, 0.31586613136402053, 0.9447300740056483, 0.9443980824873418, 0.8158851991115941,
        0.5631010340387631, 0.9407402251929046, 0.6485434876551682, 0.5631376966470001, 0.17581924875609634, 0.7033802439103178, 0.04802402495561675, 0.9183681450194972,
        0.46059317944364, 0.07964160481596883, 0.871787076270302, 0.973743142324361, 0.15923146943258415, 0.8212946080584571, 0.5415954459227064, 0.9552813822803975,
        0.4908552668172057, 0.33723691635292274, 0.46588057864910026, 0.8994239961321776, 0.09845220457674186, 0.1713400292123486, 0.39570294912818826, 0.08018956486392803,
        0.5290478278169032, 0.7141906125920976, 0.0320878067840098, 0.6412406575332606, 0.0075712007102423096, 0.7150828462386156, 0.1311989216968138, 0.4706847944253756,
        0.5447610794883336, 0.3430923933318001, 0.536082357943209, 0.4371629342483694, 0.40227962985019927, 0.3553806249465469, 0.031806622424259245, 0.7053916426174,
        0.3261570237309813, 0.419500213292063, 0.3155691223480851, 0.05664028113178088, 0.3636491555914486, 0.8502419746667123, 0.9836596530684955, 0.1628681802975801,
        0.09410832912479894, 0.28407218939480294, 0.7983417928813697, 0.24132158596506748, 0.8154729498062224, 0.29173768373895637, 0.13407102008052096, 0.18705786678800385,
        0.7167943621295573, 0.09222004247174376, 0.2319220738766018, 0.17708964382285064, 0.1391440370249517, 0.3254088083499256, 0.4013916894718289, 0.4819742663322323,
        0.15080103744648077, 0.9302407847555013, 0.9397597961319524, 0.5719200825550793, 0.9538938024682824, 0.9583882089203861, 0.5168861091262276, 0.1926396841842669,
        0.6781176744337578, 0.719366447288566
    };
    float expected_output[1*5*6*3] = {
        -0.3921688, -0.9243112, -0.29659146, -0.64000785, -0.9466343, -0.62125254, -0.71759033, -0.9171336, -0.735589, -0.34365994,
        -0.92100817, -0.23903961, -0.8962277, -0.9521279, -0.90962386, -0.7488303, -0.9563761, -0.7701762, -0.40800542, -0.87684774,
        -0.3339763, -0.6354543, -0.97068924, -0.6246325, -0.6992075, -0.9706726, -0.6818918, -0.51864433, -0.9592881, -0.51187396,
        -0.7423632, -0.89911884, -0.7457824, -0.82009757, -0.96402895, -0.8235518, -0.61980766, -0.94494647, -0.5410502, -0.8281218,
        -0.95508635, -0.8201453, -0.5937325, -0.8679507, -0.500767, -0.39430764, -0.93967676, -0.32183182, -0.58913624, -0.939717,
        -0.55179894, -0.55004454, -0.9214453, -0.4889004, -0.75294703, -0.9118363, -0.7200309, -0.3248641, -0.8878874, -0.18977344,
        -0.8873837, -0.9571257, -0.90145934, -0.50521654, -0.93739635, -0.39051685, -0.61143184, -0.9591179, -0.605999, -0.40008977,
        -0.92219675, -0.26732883, -0.19607787, -0.9172511, -0.07068595, -0.5409857, -0.9387041, -0.44181606, -0.4705004, -0.8899935,
        -0.37997037, -0.66105115, -0.89754754, -0.68141997, -0.6324047, -0.886776, -0.65066385, -0.8334821, -0.94801456, -0.83297
    };
    float *output;
    float kernel[3*3] = {
        0.56611896, -0.5144603, -0.82600045, 0.19219112, 0.3835776, -0.7475352, 0.5209291, -0.6301091, -0.99442935};
    float bias[3] = {-0.3654299, -1.5711838, -0.15546428};

    params.activation = TANH;
    params.has_bias = 1;
    params.biases = bias;
    params.input_num = 3;
    params.kernel = kernel;
    params.output_num = 3;

    operands[0].data = input;
    operands[0].dims[0] = 1;
    operands[0].dims[1] = 5;
    operands[0].dims[2] = 6;
    operands[0].dims[3] = 3;
    operands[1].data = NULL;

    input_indexes[0] = 0;
    ff_dnn_execute_layer_dense(operands, input_indexes, 1, &params, NULL);

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
    if (test())
        return 1;

    return 0;
}
