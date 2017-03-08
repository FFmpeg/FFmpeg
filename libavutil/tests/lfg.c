/*
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

#include "libavutil/log.h"
#include "libavutil/timer.h"
#include "libavutil/lfg.h"

static const double Z_TABLE[31][10] = {
    {0.5000,  0.5040,  0.5080,  0.5120,  0.5160,  0.5199,  0.5239,  0.5279,  0.5319,  0.5359},
    {0.5398,  0.5438,  0.5478,  0.5517,  0.5557,  0.5596,  0.5636,  0.5675,  0.5714,  0.5753},
    {0.5793,  0.5832,  0.5871,  0.5910,  0.5948,  0.5987,  0.6026,  0.6064,  0.6103,  0.6141},
    {0.6179,  0.6217,  0.6255,  0.6293,  0.6331,  0.6368,  0.6406,  0.6443,  0.6480,  0.6517},
    {0.6554,  0.6591,  0.6628,  0.6664,  0.6700,  0.6736,  0.6772,  0.6808,  0.6844,  0.6879},
    {0.6915,  0.6950,  0.6985,  0.7019,  0.7054,  0.7088,  0.7123,  0.7157,  0.7190,  0.7224},
    {0.7257,  0.7291,  0.7324,  0.7357,  0.7389,  0.7422,  0.7454,  0.7486,  0.7517,  0.7549},
    {0.7580,  0.7611,  0.7642,  0.7673,  0.7704,  0.7734,  0.7764,  0.7794,  0.7823,  0.7852},
    {0.7881,  0.7910,  0.7939,  0.7967,  0.7995,  0.8023,  0.8051,  0.8078,  0.8106,  0.8133},
    {0.8159,  0.8186,  0.8212,  0.8238,  0.8264,  0.8289,  0.8315,  0.8340,  0.8365,  0.8389},
    {0.8413,  0.8438,  0.8461,  0.8485,  0.8508,  0.8531,  0.8554,  0.8577,  0.8599,  0.8621},
    {0.8643,  0.8665,  0.8686,  0.8708,  0.8729,  0.8749,  0.8770,  0.8790,  0.8810,  0.8830},
    {0.8849,  0.8869,  0.8888,  0.8907,  0.8925,  0.8944,  0.8962,  0.8980,  0.8997,  0.9015},
    {0.9032,  0.9049,  0.9066,  0.9082,  0.9099,  0.9115,  0.9131,  0.9147,  0.9162,  0.9177},
    {0.9192,  0.9207,  0.9222,  0.9236,  0.9251,  0.9265,  0.9279,  0.9292,  0.9306,  0.9319},
    {0.9332,  0.9345,  0.9357,  0.9370,  0.9382,  0.9394,  0.9406,  0.9418,  0.9429,  0.9441},
    {0.9452,  0.9463,  0.9474,  0.9484,  0.9495,  0.9505,  0.9515,  0.9525,  0.9535,  0.9545},
    {0.9554,  0.9564,  0.9573,  0.9582,  0.9591,  0.9599,  0.9608,  0.9616,  0.9625,  0.9633},
    {0.9641,  0.9649,  0.9656,  0.9664,  0.9671,  0.9678,  0.9686,  0.9693,  0.9699,  0.9706},
    {0.9713,  0.9719,  0.9726,  0.9732,  0.9738,  0.9744,  0.9750,  0.9756,  0.9761,  0.9767},
    {0.9772,  0.9778,  0.9783,  0.9788,  0.9793,  0.9798,  0.9803,  0.9808,  0.9812,  0.9817},
    {0.9821,  0.9826,  0.9830,  0.9834,  0.9838,  0.9842,  0.9846,  0.9850,  0.9854,  0.9857},
    {0.9861,  0.9864,  0.9868,  0.9871,  0.9875,  0.9878,  0.9881,  0.9884,  0.9887,  0.9890},
    {0.9893,  0.9896,  0.9898,  0.9901,  0.9904,  0.9906,  0.9909,  0.9911,  0.9913,  0.9916},
    {0.9918,  0.9920,  0.9922,  0.9925,  0.9927,  0.9929,  0.9931,  0.9932,  0.9934,  0.9936},
    {0.9938,  0.9940,  0.9941,  0.9943,  0.9945,  0.9946,  0.9948,  0.9949,  0.9951,  0.9952},
    {0.9953,  0.9955,  0.9956,  0.9957,  0.9959,  0.9960,  0.9961,  0.9962,  0.9963,  0.9964},
    {0.9965,  0.9966,  0.9967,  0.9968,  0.9969,  0.9970,  0.9971,  0.9972,  0.9973,  0.9974},
    {0.9974,  0.9975,  0.9976,  0.9977,  0.9977,  0.9978,  0.9979,  0.9979,  0.9980,  0.9981},
    {0.9981,  0.9982,  0.9982,  0.9983,  0.9984,  0.9984,  0.9985,  0.9985,  0.9986,  0.9986},
    {0.9987,  0.9987,  0.9987,  0.9988,  0.9988,  0.9989,  0.9989,  0.9989,  0.9990,  0.9990} };

// Inverse cumulative distribution function
static double inv_cdf(double u)
{
    const double a[4] = { 2.50662823884,
                         -18.61500062529,
                          41.39119773534,
                         -25.44106049637};

    const double b[4] = {-8.47351093090,
                          23.08336743743,
                         -21.06224101826,
                           3.13082909833};

    const double c[9] = {0.3374754822726147,
                          0.9761690190917186,
                          0.1607979714918209,
                          0.0276438810333863,
                          0.0038405729373609,
                          0.0003951896511919,
                          0.0000321767881768,
                          0.0000002888167364,
                          0.0000003960315187};

    double r;
    double x = u - 0.5;

    // Beasley-Springer
     if (fabs(x) < 0.42) {

        double y = x * x;
        r        = x * (((a[3]*y+a[2])*y+a[1])*y+a[0]) /
                        ((((b[3]*y+b[2])*y+b[1])*y+b[0])*y+1.0);
    }
    else {// Moro
        r = u;
        if (x > 0.0)
            r = 1.0 - u;
        r = log(-log(r));
        r = c[0] + r*(c[1]+r*(c[2]+r*(c[3]+r*(c[4]+r*(c[5]+r*(c[6]+
                                                        r*(c[7]+r*c[8])))))));
        if (x < 0.0)
            r = -r;
    }

    return r;
}
int main(void)
{
    int x = 0;
    int i, j;
    AVLFG state;
    av_lfg_init(&state, 0xdeadbeef);
    for (j = 0; j < 10000; j++) {
        for (i = 0; i < 624; i++) {
            //av_log(NULL, AV_LOG_ERROR, "%X\n", av_lfg_get(&state));
            x += av_lfg_get(&state);
        }
    }
    av_log(NULL, AV_LOG_ERROR, "final value:%X\n", x);

    /* BMG usage example */
    {
        double mean   = 1000;
        double stddev = 53;
        double samp_mean = 0.0, samp_stddev = 0.0, QH = 0;
        double Z, p_value = -1, tot_samp = 1000;
        double *PRN_arr = av_malloc_array(tot_samp, sizeof(double));

        if (!PRN_arr) {
            fprintf(stderr, "failed to allocate memory!\n");
            return 1;
        }

        av_lfg_init(&state, 42);
        for (i = 0; i < tot_samp; i += 2) {
            double bmg_out[2];
            av_bmg_get(&state, bmg_out);
            PRN_arr[i  ] = bmg_out[0] * stddev + mean;
            PRN_arr[i+1] = bmg_out[1] * stddev + mean;
            samp_mean   += PRN_arr[i] + PRN_arr[i+1];
            samp_stddev += PRN_arr[i] * PRN_arr[i] + PRN_arr[i+1] * PRN_arr[i+1];
            printf("PRN%d : %f\n"
                   "PRN%d : %f\n",
                   i, PRN_arr[i], i+1, PRN_arr[i+1]);
        }
        samp_mean /= tot_samp;
        samp_stddev /= (tot_samp - 1);
        samp_stddev -= (tot_samp * 1.0 / (tot_samp - 1))*samp_mean*samp_mean;
        samp_stddev = sqrt(samp_stddev);
        Z = (mean - samp_mean) / (stddev / sqrt(tot_samp));
        {
            int x, y, a, b, flag = 0;

            if (Z < 0.0) {
                flag = !flag;
                Z = Z * -1.0;
            }

            a = (int)(Z * 100);
            b = ((int)Z * 100);
            x = Z * 10;
            y = (b > 0) ? a % b : a;
            y = y % 10;
            if (x > 30 || y > 9) {
                av_log(NULL, AV_LOG_INFO, "error: out of bounds! tried to access"
                                          "Z_TABLE[%d][%d]\n", x, y);
                goto SKIP;
            }
            p_value = flag ? 1 - Z_TABLE[x][y] : Z_TABLE[x][y];
        }

SKIP:   for (i = 0; i < tot_samp; ++i) {

            if ( i < (tot_samp - 1)) {
                double H_diff;
                H_diff  = inv_cdf((i + 2.0 - (3.0/8.0)) / (tot_samp + (1.0/4.0)));
                H_diff -= inv_cdf((i + 1.0 - (3.0/8.0)) / (tot_samp + (1.0/4.0)));

                QH     += ((PRN_arr[i + 1] - PRN_arr[i]) / H_diff);
            }
        }
        QH = 1.0 - QH / ((tot_samp - 1.0) * samp_stddev);

        printf("sample mean  : %f\n"
               "true mean    : %f\n"
               "sample stddev: %f\n"
               "true stddev  : %f\n"
               "z-score      : %f\n"
               "p-value      : %f\n"
               "QH[normality]: %f\n",
               samp_mean, mean, samp_stddev, stddev, Z, p_value, QH);

        av_freep(&PRN_arr);
    }
    return 0;
}
