/*
 * Copyright (c) 2015 Kevin Wheatley <kevin.j.wheatley@gmail.com>
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

#include <stddef.h>
#include <math.h>

#include "common.h"
#include "libavutil/color_utils.h"
#include "libavutil/pixfmt.h"

double avpriv_get_gamma_from_trc(enum AVColorTransferCharacteristic trc)
{
    double gamma;
    switch (trc) {
        case AVCOL_TRC_BT709:
        case AVCOL_TRC_SMPTE170M:
        case AVCOL_TRC_SMPTE240M:
        case AVCOL_TRC_BT1361_ECG:
        case AVCOL_TRC_BT2020_10:
        case AVCOL_TRC_BT2020_12:
            /* these share a segmented TRC, but gamma 1.961 is a close
              approximation, and also more correct for decoding content */
            gamma = 1.961;
            break;
        case AVCOL_TRC_GAMMA22:
        case AVCOL_TRC_IEC61966_2_1:
            gamma = 2.2;
            break;
        case AVCOL_TRC_GAMMA28:
            gamma = 2.8;
            break;
        case AVCOL_TRC_LINEAR:
            gamma = 1.0;
            break;
        default:
            gamma = 0.0; // Unknown value representation
    }
    return gamma;
}

#define BT709_alpha 1.099296826809442
#define BT709_beta 0.018053968510807

static double avpriv_trc_bt709(double Lc)
{
    const double a = BT709_alpha;
    const double b = BT709_beta;

    return (0.0 > Lc) ? 0.0
         : (  b > Lc) ? 4.500 * Lc
         :              a * pow(Lc, 0.45) - (a - 1.0);
}

static double avpriv_trc_gamma22(double Lc)
{
    return (0.0 > Lc) ? 0.0 : pow(Lc, 1.0/ 2.2);
}

static double avpriv_trc_gamma28(double Lc)
{
    return (0.0 > Lc) ? 0.0 : pow(Lc, 1.0/ 2.8);
}

static double avpriv_trc_smpte240M(double Lc)
{
    const double a = 1.1115;
    const double b = 0.0228;

    return (0.0 > Lc) ? 0.0
         : (  b > Lc) ? 4.000 * Lc
         :              a * pow(Lc, 0.45) - (a - 1.0);
}

static double avpriv_trc_linear(double Lc)
{
    return Lc;
}

static double avpriv_trc_log(double Lc)
{
    return (0.01 > Lc) ? 0.0 : 1.0 + log10(Lc) / 2.0;
}

static double avpriv_trc_log_sqrt(double Lc)
{
    // sqrt(10) / 1000
    return (0.00316227766 > Lc) ? 0.0 : 1.0 + log10(Lc) / 2.5;
}

static double avpriv_trc_iec61966_2_4(double Lc)
{
    const double a = BT709_alpha;
    const double b = BT709_beta;

    return (-b >= Lc) ? -a * pow(-Lc, 0.45) + (a - 1.0)
         : ( b >  Lc) ? 4.500 * Lc
         :               a * pow( Lc, 0.45) - (a - 1.0);
}

static double avpriv_trc_bt1361(double Lc)
{
    const double a = BT709_alpha;
    const double b = BT709_beta;

    return (-0.0045 >= Lc) ? -(a * pow(-4.0 * Lc, 0.45) + (a - 1.0)) / 4.0
         : ( b >  Lc) ? 4.500 * Lc
         :               a * pow( Lc, 0.45) - (a - 1.0);
}

static double avpriv_trc_iec61966_2_1(double Lc)
{
    const double a = 1.055;
    const double b = 0.0031308;

    return (0.0 > Lc) ? 0.0
         : (  b > Lc) ? 12.92 * Lc
         :              a * pow(Lc, 1.0  / 2.4) - (a - 1.0);
}

static double avpriv_trc_smpte_st2084(double Lc)
{
    const double c1 =         3424.0 / 4096.0; // c3-c2 + 1
    const double c2 =  32.0 * 2413.0 / 4096.0;
    const double c3 =  32.0 * 2392.0 / 4096.0;
    const double m  = 128.0 * 2523.0 / 4096.0;
    const double n  =  0.25 * 2610.0 / 4096.0;
    const double L  = Lc / 10000.0;
    const double Ln = pow(L, n);

    return (0.0 > Lc) ? 0.0
         :              pow((c1 + c2 * Ln) / (1.0 + c3 * Ln), m);

}

static double avpriv_trc_smpte_st428_1(double Lc)
{
    return (0.0 > Lc) ? 0.0
         :              pow(48.0 * Lc / 52.37, 1.0 / 2.6);
}


static double avpriv_trc_arib_std_b67(double Lc) {
    // The function uses the definition from HEVC, which assumes that the peak
    // white is input level = 1. (this is equivalent to scaling E = Lc * 12 and
    // using the definition from the ARIB STD-B67 spec)
    const double a = 0.17883277;
    const double b = 0.28466892;
    const double c = 0.55991073;
    return (0.0 > Lc) ? 0.0 :
        (Lc <= 1.0 / 12.0 ? sqrt(3.0 * Lc) : a * log(12.0 * Lc - b) + c);
}

avpriv_trc_function avpriv_get_trc_function_from_trc(enum AVColorTransferCharacteristic trc)
{
    avpriv_trc_function func = NULL;
    switch (trc) {
        case AVCOL_TRC_BT709:
        case AVCOL_TRC_SMPTE170M:
        case AVCOL_TRC_BT2020_10:
        case AVCOL_TRC_BT2020_12:
            func = avpriv_trc_bt709;
            break;

        case AVCOL_TRC_GAMMA22:
            func = avpriv_trc_gamma22;
            break;
        case AVCOL_TRC_GAMMA28:
            func = avpriv_trc_gamma28;
            break;

        case AVCOL_TRC_SMPTE240M:
            func = avpriv_trc_smpte240M;
            break;

        case AVCOL_TRC_LINEAR:
            func = avpriv_trc_linear;
            break;

        case AVCOL_TRC_LOG:
            func = avpriv_trc_log;
            break;

        case AVCOL_TRC_LOG_SQRT:
            func = avpriv_trc_log_sqrt;
            break;

        case AVCOL_TRC_IEC61966_2_4:
            func = avpriv_trc_iec61966_2_4;
            break;

        case AVCOL_TRC_BT1361_ECG:
            func = avpriv_trc_bt1361;
            break;

        case AVCOL_TRC_IEC61966_2_1:
            func = avpriv_trc_iec61966_2_1;
            break;

        case AVCOL_TRC_SMPTEST2084:
            func = avpriv_trc_smpte_st2084;
            break;

        case AVCOL_TRC_SMPTEST428_1:
            func = avpriv_trc_smpte_st428_1;
            break;

        case AVCOL_TRC_ARIB_STD_B67:
            func = avpriv_trc_arib_std_b67;
            break;

        case AVCOL_TRC_RESERVED0:
        case AVCOL_TRC_UNSPECIFIED:
        case AVCOL_TRC_RESERVED:
        default:
            break;
    }
    return func;
}
