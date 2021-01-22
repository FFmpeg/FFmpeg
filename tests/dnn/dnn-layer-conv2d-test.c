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
#include "libavfilter/dnn/dnn_backend_native_layer_conv2d.h"

#define EPSON 0.00001

static int test_with_same_dilate(void)
{
    // the input data and expected data are generated with below python code.
    /*
    x = tf.placeholder(tf.float32, shape=[1, None, None, 3])
    y = tf.layers.conv2d(x, 2, 3, activation=tf.nn.tanh, padding='same', dilation_rate=(2, 2), bias_initializer=tf.keras.initializers.he_normal())
    data = np.random.rand(1, 5, 6, 3);

    sess=tf.Session()
    sess.run(tf.global_variables_initializer())

    weights = dict([(var.name, sess.run(var)) for var in tf.trainable_variables()])
    kernel = weights['conv2d/kernel:0']
    kernel = np.transpose(kernel, [3, 0, 1, 2])
    print("kernel:")
    print(kernel.shape)
    print(list(kernel.flatten()))

    bias = weights['conv2d/bias:0']
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

    ConvolutionalParams params;
    DnnOperand operands[2];
    int32_t input_indexes[1];
    float input[1*5*6*3] = {
        0.7012556460308194, 0.4233847954643357, 0.19515900664313612, 0.16343083004926495, 0.5758261611052848, 0.9510767434014871, 0.11014085055947687,
        0.906327053637727, 0.8136794715542507, 0.45371764543639526, 0.5768443343523952, 0.19543668786046986, 0.15648326047898609, 0.2099500241141279,
        0.17658777090552413, 0.059335724777169196, 0.1729991838469117, 0.8150514704819208, 0.4435535466703049, 0.3752188477566878, 0.749936650421431,
        0.6823494635284907, 0.10776389679424747, 0.34247481674596836, 0.5147867256244629, 0.9063709728129032, 0.12423605800856818, 0.6064872945412728,
        0.5891681538551459, 0.9865836236466314, 0.9002163879294677, 0.003968273184274618, 0.8628374809643967, 0.1327176268279583, 0.8449799925703798,
        0.1937671869354366, 0.41524410152707425, 0.02038786604756837, 0.49792466069597496, 0.8881874553848784, 0.9683921035597336, 0.4122972568010813,
        0.843553550993252, 0.9588482762501964, 0.5190350762645546, 0.4283584264145317, 0.09781496073714646, 0.9501058833776156, 0.8665541760152776,
        0.31669272550095806, 0.07133074675453632, 0.606438007334886, 0.7007157020538224, 0.4827996264130444, 0.5167615606392761, 0.6385043039312651,
        0.23069664707810555, 0.058233497329354456, 0.06323892961591071, 0.24816458893245974, 0.8646369065257812, 0.24742185893094837, 0.09991225948167437,
        0.625700606979606, 0.7678541502111257, 0.6215834594679912, 0.5623003956582483, 0.07389123942681242, 0.7659100715711249, 0.486061471642225,
        0.9947455699829012, 0.9094911797643259, 0.7644355876253265, 0.05384315321492239, 0.13565394382783613, 0.9810628204953316, 0.007386389078887889,
        0.226182754156241, 0.2609021390764772, 0.24182802076928933, 0.13264782451941648, 0.2035816485767682, 0.005504188177612557, 0.7014619934040155,
        0.956215988391991, 0.5670398541013633, 0.9809764721750784, 0.6886338100487461, 0.5758152317218274, 0.7137823176776179
    };
    float expected_output[1*5*6*2] = {
        -0.9480655, -0.7169147, -0.9404794, -0.5567385, -0.8991124, -0.8306558, -0.94487447, -0.8932543, -0.88238764, -0.7301602,
        -0.8974813, -0.7026703, -0.8858988, -0.53203243, -0.92881465, -0.5648504, -0.8871471, -0.7000097, -0.91754407, -0.79684794,
        -0.760465, -0.117928326, -0.88302773, -0.8975289, -0.70615053, 0.19231977, -0.8318776, -0.386184, -0.80698484, -0.8556624,
        -0.7336671, -0.6168619, -0.7658234, -0.63449603, -0.73314047, -0.87502456, -0.58158904, -0.4184259, -0.52618927, -0.13613208,
        -0.5093187, -0.21027721, -0.39455596, -0.44507834, -0.22269244, -0.73400885, -0.77655095, -0.74408925, -0.57313335, -0.15333457,
        -0.74620694, -0.34858236, -0.42586932, -0.5240488, 0.1634339, -0.2447881, -0.57927346, -0.62732303, -0.82287043, -0.8474058
    };
    float *output;
    float kernel[2*3*3*3] = {
        0.26025516, 0.16536498, -0.24351254, 0.33892477, -0.34005195, 0.35202783, 0.34056443, 0.01422739, 0.13799345, 0.29489166,
        0.2781723, 0.178585, 0.22122234, 0.044115514, 0.13134438, 0.31705368, 0.22527462, -0.021323413, 0.115134746, -0.18216397,
        -0.21197563, -0.027848959, -0.01704529, -0.12401503, -0.23415318, -0.12661739, -0.35338148, 0.20049328, -0.076153606,
        -0.23642601, -0.3125769, -0.025851756, -0.30006272, 0.050762743, 0.32003498, 0.3052225, -0.0017385483, 0.25337684, -0.25664508,
        0.27846587, -0.3112659, 0.2066065, 0.31499845, 0.113178134, 0.09449363, -0.11828774, -0.12671001, -0.36259216, 0.2710235,
        -0.19676702, 0.023612618, -0.2596915, -0.34949252, -0.108270735
    };
    float bias[2] = { -1.6574852, -0.72915393 };

    NativeContext ctx;
    ctx.class = NULL;
    ctx.options.conv2d_threads = 1;

    params.activation = TANH;
    params.has_bias = 1;
    params.biases = bias;
    params.dilation = 2;
    params.input_num = 3;
    params.kernel = kernel;
    params.kernel_size = 3;
    params.output_num = 2;
    params.padding_method = SAME;

    operands[0].data = input;
    operands[0].dims[0] = 1;
    operands[0].dims[1] = 5;
    operands[0].dims[2] = 6;
    operands[0].dims[3] = 3;
    operands[1].data = NULL;

    input_indexes[0] = 0;
    ff_dnn_execute_layer_conv2d(operands, input_indexes, 1, &params, &ctx);

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

static int test_with_valid(void)
{
    // the input data and expected data are generated with below python code.
    /*
    x = tf.placeholder(tf.float32, shape=[1, None, None, 3])
    y = tf.layers.conv2d(x, 2, 3, activation=tf.nn.tanh, padding='valid', bias_initializer=tf.keras.initializers.he_normal())
    data = np.random.rand(1, 5, 6, 3);

    sess=tf.Session()
    sess.run(tf.global_variables_initializer())

    weights = dict([(var.name, sess.run(var)) for var in tf.trainable_variables()])
    kernel = weights['conv2d/kernel:0']
    kernel = np.transpose(kernel, [3, 0, 1, 2])
    print("kernel:")
    print(kernel.shape)
    print(list(kernel.flatten()))

    bias = weights['conv2d/bias:0']
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

    ConvolutionalParams params;
    DnnOperand operands[2];
    int32_t input_indexes[1];
    float input[1*5*6*3] = {
        0.26126657468269665, 0.42762216215337556, 0.7466274030131497, 0.802550266787863, 0.3709323443076644, 0.5919817068197668, 0.49274512279324967,
        0.7170132295090351, 0.0911793215410649, 0.5134213878288361, 0.670132600785118, 0.49417034512633484, 0.03887389460089885, 0.436785102836845,
        0.1490231658611978, 0.6413606121498127, 0.8595987991375995, 0.9132593077586231, 0.7075959004873255, 0.17754995944845464, 0.5212507214937141,
        0.35379732738215475, 0.25205107358505296, 0.3928792840544273, 0.09485294189485782, 0.8685115437448666, 0.6489046799288605, 0.509253797582924,
        0.8993255536791972, 0.18740056466602373, 0.34237617336313986, 0.3871438962989183, 0.1488532571774911, 0.5187002331293636, 0.8137098818752955,
        0.521761863717401, 0.4622312310118274, 0.29038411334638825, 0.16194915718170566, 0.5175999923925211, 0.8852230040101133, 0.0218263385047206,
        0.08482355352852367, 0.3463638568376264, 0.28627127120619733, 0.9553293378948409, 0.4803391055970835, 0.841635695030805, 0.3556828280031952,
        0.06778527221541808, 0.28193560357091596, 0.8399957619031576, 0.03305536359456385, 0.6625039162109645, 0.9300552020023897, 0.8551529138204146,
        0.6133216915522418, 0.222427800857393, 0.1315422686800336, 0.6189144989185527, 0.5346184916866876, 0.8348888624532548, 0.6544834567840291,
        0.2844062293389934, 0.28780026600883324, 0.5372272015684924, 0.6250226011503823, 0.28119106062279453, 0.49655812908420094, 0.6451488959145951,
        0.7362580606834843, 0.44815578616664087, 0.6454760235835586, 0.6794062414265861, 0.045378883014935756, 0.9008388543865096, 0.7949752851269782,
        0.4179928876222264, 0.28733419007048644, 0.996902319501908, 0.5690851338677467, 0.9511814013279738, 0.025323788678181636, 0.5594359732604794,
        0.1213732595086251, 0.7172624313368294, 0.6759328959074691, 0.07252138454885071, 0.17557735158403442, 0.5988895455048769
    };
    float expected_output[1*3*4*2] = {
        -0.556947, -0.42143887, -0.092070885, 0.27404794, -0.41886684, 0.0862887, -0.25001016, -0.342721, 0.020730592, 0.04016919, -0.69839877,
        -0.06136704, 0.14186388, -0.11655602, -0.23489095, -0.3845829, -0.19017771, 0.1595885, -0.18308741, -0.3071209, -0.5848686, -0.22509028,
        -0.6023201, -0.14448485
    };
    float *output;
    float kernel[2*3*3*3] = {
        -0.25291282, 0.22402048, 0.028642118, -0.14615723, -0.27362752, -0.34801802, -0.2759148, 0.19594926, -0.25029412, 0.34606284, 0.10376671,
        -0.1015394, 0.23616093, 0.2134214, 0.35285157, 0.05893758, 0.0024731457, -0.17143056, 0.35758412, 0.2186206, -0.28384736, -0.21206513,
        -0.20871592, 0.27070445, 0.25878823, 0.11136332, -0.33737376, 0.08353335, -0.34290665, 0.041805506, -0.09738535, 0.3284936, -0.16838405,
        -0.032494456, -0.29193437, 0.033259362, -0.09272635, -0.2802651, -0.28648436, 0.3542878, 0.2432127, -0.24551713, 0.27813476, 0.21024024,
        -0.013690501, -0.1350077, -0.07826337, -0.34563828, 0.3220685, -0.07571727, 0.19420576, 0.20783454, 0.18738335, 0.16672492
    };
    float bias[2] = { -0.4773722, -0.19620377 };

    NativeContext ctx;
    ctx.class = NULL;
    ctx.options.conv2d_threads = 1;

    params.activation = TANH;
    params.has_bias = 1;
    params.biases = bias;
    params.dilation = 1;
    params.input_num = 3;
    params.kernel = kernel;
    params.kernel_size = 3;
    params.output_num = 2;
    params.padding_method = VALID;

    operands[0].data = input;
    operands[0].dims[0] = 1;
    operands[0].dims[1] = 5;
    operands[0].dims[2] = 6;
    operands[0].dims[3] = 3;
    operands[1].data = NULL;

    input_indexes[0] = 0;
    ff_dnn_execute_layer_conv2d(operands, input_indexes, 1, &params, &ctx);

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
    if (test_with_valid())
        return 1;
    if (test_with_same_dilate())
        return 1;

    return 0;
}
