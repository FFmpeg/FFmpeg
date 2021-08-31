/*
 * Copyright (c) 2020 Paul B Mahol
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

#include <float.h>

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "audio.h"
#include "filters.h"
#include "formats.h"

enum WaveletTypes {
    SYM2,
    SYM4,
    RBIOR68,
    DEB10,
    SYM10,
    COIF5,
    BL3,
    NB_WAVELET_TYPES,
};

/*
 * All wavelets coefficients are taken from: http://wavelets.pybytes.com/
 */

static const double bl3_lp[42] = {
    0.000146098, -0.000232304, -0.000285414, 0.000462093, 0.000559952,
    -0.000927187, -0.001103748, 0.00188212, 0.002186714, -0.003882426,
    -0.00435384, 0.008201477, 0.008685294, -0.017982291, -0.017176331,
    0.042068328, 0.032080869, -0.110036987, -0.050201753, 0.433923147,
    0.766130398, 0.433923147, -0.050201753, -0.110036987, 0.032080869,
    0.042068328, -0.017176331, -0.017982291, 0.008685294, 0.008201477,
    -0.00435384, -0.003882426, 0.002186714, 0.00188212, -0.001103748,
    -0.000927187, 0.000559952, 0.000462093, -0.000285414, -0.000232304,
    0.000146098, 0.0,
};

static const double bl3_hp[42] = {
    0.0, 0.000146098, 0.000232304, -0.000285414, -0.000462093, 0.000559952,
    0.000927187, -0.001103748, -0.00188212, 0.002186714, 0.003882426,
    -0.00435384, -0.008201477, 0.008685294, 0.017982291, -0.017176331,
    -0.042068328, 0.032080869, 0.110036987, -0.050201753, -0.433923147,
    0.766130398, -0.433923147, -0.050201753, 0.110036987, 0.032080869,
    -0.042068328, -0.017176331, 0.017982291, 0.008685294, -0.008201477,
    -0.00435384, 0.003882426, 0.002186714, -0.00188212, -0.001103748,
    0.000927187, 0.000559952, -0.000462093, -0.000285414, 0.000232304,
    0.000146098,
};

static const double bl3_ilp[42] = {
    0.0, 0.000146098, -0.000232304, -0.000285414, 0.000462093, 0.000559952,
    -0.000927187, -0.001103748, 0.00188212, 0.002186714, -0.003882426,
    -0.00435384, 0.008201477, 0.008685294, -0.017982291, -0.017176331,
    0.042068328, 0.032080869, -0.110036987, -0.050201753, 0.433923147,
    0.766130398, 0.433923147, -0.050201753, -0.110036987, 0.032080869,
    0.042068328, -0.017176331, -0.017982291, 0.008685294, 0.008201477,
    -0.00435384, -0.003882426, 0.002186714, 0.00188212, -0.001103748,
    -0.000927187, 0.000559952, 0.000462093, -0.000285414, -0.000232304,
    0.000146098,
};

static const double bl3_ihp[42] = {
    0.000146098, 0.000232304, -0.000285414, -0.000462093, 0.000559952,
    0.000927187, -0.001103748, -0.00188212, 0.002186714, 0.003882426,
    -0.00435384, -0.008201477, 0.008685294, 0.017982291, -0.017176331,
    -0.042068328, 0.032080869, 0.110036987, -0.050201753, -0.433923147,
    0.766130398, -0.433923147, -0.050201753, 0.110036987, 0.032080869,
    -0.042068328, -0.017176331, 0.017982291, 0.008685294, -0.008201477,
    -0.00435384, 0.003882426, 0.002186714, -0.00188212, -0.001103748,
    0.000927187, 0.000559952, -0.000462093, -0.000285414, 0.000232304,
    0.000146098,
};

static const double sym10_lp[20] = {
    0.0007701598091144901, 9.563267072289475e-05,
    -0.008641299277022422, -0.0014653825813050513,
    0.0459272392310922, 0.011609893903711381,
    -0.15949427888491757, -0.07088053578324385,
    0.47169066693843925, 0.7695100370211071,
    0.38382676106708546, -0.03553674047381755,
    -0.0319900568824278, 0.04999497207737669,
    0.005764912033581909, -0.02035493981231129,
    -0.0008043589320165449, 0.004593173585311828,
    5.7036083618494284e-05, -0.0004593294210046588,
};

static const double sym10_hp[20] = {
    0.0004593294210046588, 5.7036083618494284e-05,
    -0.004593173585311828, -0.0008043589320165449,
    0.02035493981231129, 0.005764912033581909,
    -0.04999497207737669, -0.0319900568824278,
    0.03553674047381755, 0.38382676106708546,
    -0.7695100370211071, 0.47169066693843925,
    0.07088053578324385, -0.15949427888491757,
    -0.011609893903711381, 0.0459272392310922,
    0.0014653825813050513, -0.008641299277022422,
    -9.563267072289475e-05, 0.0007701598091144901,
};

static const double sym10_ilp[20] = {
    -0.0004593294210046588, 5.7036083618494284e-05,
    0.004593173585311828, -0.0008043589320165449,
    -0.02035493981231129, 0.005764912033581909,
    0.04999497207737669, -0.0319900568824278,
    -0.03553674047381755, 0.38382676106708546,
    0.7695100370211071, 0.47169066693843925,
    -0.07088053578324385, -0.15949427888491757,
    0.011609893903711381, 0.0459272392310922,
    -0.0014653825813050513, -0.008641299277022422,
    9.563267072289475e-05, 0.0007701598091144901,
};

static const double sym10_ihp[20] = {
    0.0007701598091144901, -9.563267072289475e-05,
    -0.008641299277022422, 0.0014653825813050513,
    0.0459272392310922, -0.011609893903711381,
    -0.15949427888491757, 0.07088053578324385,
    0.47169066693843925, -0.7695100370211071,
    0.38382676106708546, 0.03553674047381755,
    -0.0319900568824278, -0.04999497207737669,
    0.005764912033581909, 0.02035493981231129,
    -0.0008043589320165449, -0.004593173585311828,
    5.7036083618494284e-05, 0.0004593294210046588,
};

static const double rbior68_lp[18] = {
    0.0, 0.0, 0.0, 0.0,
    0.014426282505624435, 0.014467504896790148,
    -0.07872200106262882, -0.04036797903033992,
    0.41784910915027457, 0.7589077294536541,
    0.41784910915027457, -0.04036797903033992,
    -0.07872200106262882, 0.014467504896790148,
    0.014426282505624435, 0.0, 0.0, 0.0,
};

static const double rbior68_hp[18] = {
    -0.0019088317364812906, -0.0019142861290887667,
    0.016990639867602342, 0.01193456527972926,
    -0.04973290349094079, -0.07726317316720414,
    0.09405920349573646, 0.4207962846098268,
    -0.8259229974584023, 0.4207962846098268,
    0.09405920349573646, -0.07726317316720414,
    -0.04973290349094079, 0.01193456527972926,
    0.016990639867602342, -0.0019142861290887667,
    -0.0019088317364812906, 0.0,
};

static const double rbior68_ilp[18] = {
    0.0019088317364812906, -0.0019142861290887667,
    -0.016990639867602342, 0.01193456527972926,
    0.04973290349094079, -0.07726317316720414,
    -0.09405920349573646, 0.4207962846098268,
    0.8259229974584023, 0.4207962846098268,
    -0.09405920349573646, -0.07726317316720414,
    0.04973290349094079, 0.01193456527972926,
    -0.016990639867602342, -0.0019142861290887667,
    0.0019088317364812906, 0.0,
};

static const double rbior68_ihp[18] = {
    0.0, 0.0, 0.0, 0.0,
    0.014426282505624435, -0.014467504896790148,
    -0.07872200106262882, 0.04036797903033992,
    0.41784910915027457, -0.7589077294536541,
    0.41784910915027457, 0.04036797903033992,
    -0.07872200106262882, -0.014467504896790148,
    0.014426282505624435, 0.0, 0.0, 0.0,
};

static const double coif5_lp[30] = {
    -9.517657273819165e-08, -1.6744288576823017e-07,
    2.0637618513646814e-06, 3.7346551751414047e-06,
    -2.1315026809955787e-05, -4.134043227251251e-05,
    0.00014054114970203437, 0.00030225958181306315,
    -0.0006381313430451114, -0.0016628637020130838,
    0.0024333732126576722, 0.006764185448053083,
    -0.009164231162481846, -0.01976177894257264,
    0.03268357426711183, 0.0412892087501817,
    -0.10557420870333893, -0.06203596396290357,
    0.4379916261718371, 0.7742896036529562,
    0.4215662066908515, -0.05204316317624377,
    -0.09192001055969624, 0.02816802897093635,
    0.023408156785839195, -0.010131117519849788,
    -0.004159358781386048, 0.0021782363581090178,
    0.00035858968789573785, -0.00021208083980379827,
};

static const double coif5_hp[30] = {
    0.00021208083980379827, 0.00035858968789573785,
    -0.0021782363581090178, -0.004159358781386048,
    0.010131117519849788, 0.023408156785839195,
    -0.02816802897093635, -0.09192001055969624,
    0.05204316317624377, 0.4215662066908515,
    -0.7742896036529562, 0.4379916261718371,
    0.06203596396290357, -0.10557420870333893,
    -0.0412892087501817, 0.03268357426711183,
    0.01976177894257264, -0.009164231162481846,
    -0.006764185448053083, 0.0024333732126576722,
    0.0016628637020130838, -0.0006381313430451114,
    -0.00030225958181306315, 0.00014054114970203437,
    4.134043227251251e-05, -2.1315026809955787e-05,
    -3.7346551751414047e-06, 2.0637618513646814e-06,
    1.6744288576823017e-07, -9.517657273819165e-08,
};

static const double coif5_ilp[30] = {
    -0.00021208083980379827, 0.00035858968789573785,
    0.0021782363581090178, -0.004159358781386048,
    -0.010131117519849788, 0.023408156785839195,
    0.02816802897093635, -0.09192001055969624,
    -0.05204316317624377, 0.4215662066908515,
    0.7742896036529562, 0.4379916261718371,
    -0.06203596396290357, -0.10557420870333893,
    0.0412892087501817, 0.03268357426711183,
    -0.01976177894257264, -0.009164231162481846,
    0.006764185448053083, 0.0024333732126576722,
    -0.0016628637020130838, -0.0006381313430451114,
    0.00030225958181306315, 0.00014054114970203437,
    -4.134043227251251e-05, -2.1315026809955787e-05,
    3.7346551751414047e-06, 2.0637618513646814e-06,
    -1.6744288576823017e-07, -9.517657273819165e-08,
};

static const double coif5_ihp[30] = {
    -9.517657273819165e-08, 1.6744288576823017e-07,
    2.0637618513646814e-06, -3.7346551751414047e-06,
    -2.1315026809955787e-05, 4.134043227251251e-05,
    0.00014054114970203437, -0.00030225958181306315,
    -0.0006381313430451114, 0.0016628637020130838,
    0.0024333732126576722, -0.006764185448053083,
    -0.009164231162481846, 0.01976177894257264,
    0.03268357426711183, -0.0412892087501817,
    -0.10557420870333893, 0.06203596396290357,
    0.4379916261718371, -0.7742896036529562,
    0.4215662066908515, 0.05204316317624377,
    -0.09192001055969624, -0.02816802897093635,
    0.023408156785839195, 0.010131117519849788,
    -0.004159358781386048, -0.0021782363581090178,
    0.00035858968789573785, 0.00021208083980379827,
};

static const double deb10_lp[20] = {
    -1.326420300235487e-05, 9.358867000108985e-05,
    -0.0001164668549943862, -0.0006858566950046825,
    0.00199240529499085, 0.0013953517469940798,
    -0.010733175482979604, 0.0036065535669883944,
    0.03321267405893324, -0.02945753682194567,
    -0.07139414716586077, 0.09305736460380659,
    0.12736934033574265, -0.19594627437659665,
    -0.24984642432648865, 0.2811723436604265,
    0.6884590394525921, 0.5272011889309198,
    0.18817680007762133, 0.026670057900950818,
};

static const double deb10_hp[20] = {
    -0.026670057900950818, 0.18817680007762133,
    -0.5272011889309198, 0.6884590394525921,
    -0.2811723436604265, -0.24984642432648865,
    0.19594627437659665, 0.12736934033574265,
    -0.09305736460380659, -0.07139414716586077,
    0.02945753682194567, 0.03321267405893324,
    -0.0036065535669883944, -0.010733175482979604,
    -0.0013953517469940798, 0.00199240529499085,
    0.0006858566950046825, -0.0001164668549943862,
    -9.358867000108985e-05, -1.326420300235487e-05,
};

static const double deb10_ilp[20] = {
    0.026670057900950818, 0.18817680007762133,
    0.5272011889309198, 0.6884590394525921,
    0.2811723436604265, -0.24984642432648865,
    -0.19594627437659665, 0.12736934033574265,
    0.09305736460380659, -0.07139414716586077,
    -0.02945753682194567, 0.03321267405893324,
    0.0036065535669883944, -0.010733175482979604,
    0.0013953517469940798, 0.00199240529499085,
    -0.0006858566950046825, -0.0001164668549943862,
    9.358867000108985e-05, -1.326420300235487e-05,
};

static const double deb10_ihp[20] = {
    -1.326420300235487e-05, -9.358867000108985e-05,
    -0.0001164668549943862, 0.0006858566950046825,
    0.00199240529499085, -0.0013953517469940798,
    -0.010733175482979604, -0.0036065535669883944,
    0.03321267405893324, 0.02945753682194567,
    -0.07139414716586077, -0.09305736460380659,
    0.12736934033574265, 0.19594627437659665,
    -0.24984642432648865, -0.2811723436604265,
    0.6884590394525921, -0.5272011889309198,
    0.18817680007762133, -0.026670057900950818,
};

static const double sym4_lp[8] = {
    -0.07576571478927333,
    -0.02963552764599851,
    0.49761866763201545,
    0.8037387518059161,
    0.29785779560527736,
    -0.09921954357684722,
    -0.012603967262037833,
    0.0322231006040427,
};

static const double sym4_hp[8] = {
    -0.0322231006040427,
    -0.012603967262037833,
    0.09921954357684722,
    0.29785779560527736,
    -0.8037387518059161,
    0.49761866763201545,
    0.02963552764599851,
    -0.07576571478927333,
};

static const double sym4_ilp[8] = {
    0.0322231006040427,
    -0.012603967262037833,
    -0.09921954357684722,
    0.29785779560527736,
    0.8037387518059161,
    0.49761866763201545,
    -0.02963552764599851,
    -0.07576571478927333,
};

static const double sym4_ihp[8] = {
    -0.07576571478927333,
    0.02963552764599851,
    0.49761866763201545,
    -0.8037387518059161,
    0.29785779560527736,
    0.09921954357684722,
    -0.012603967262037833,
    -0.0322231006040427,
};

static const double sym2_lp[4] = {
    -0.12940952255092145, 0.22414386804185735,
    0.836516303737469, 0.48296291314469025,
};

static const double sym2_hp[4] = {
    -0.48296291314469025, 0.836516303737469,
    -0.22414386804185735, -0.12940952255092145,
};

static const double sym2_ilp[4] = {
    0.48296291314469025, 0.836516303737469,
    0.22414386804185735, -0.12940952255092145,
};

static const double sym2_ihp[4] = {
    -0.12940952255092145, -0.22414386804185735,
    0.836516303737469, -0.48296291314469025,
};

#define MAX_LEVELS 13

typedef struct ChannelParams {
    int *output_length;
    int *filter_length;
    double **output_coefs;
    double **subbands_to_free;
    double **filter_coefs;

    int tempa_length;
    int tempa_len_max;
    int temp_in_length;
    int temp_in_max_length;
    int buffer_length;
    int min_left_ext;
    int max_left_ext;

    double *tempa;
    double *tempd;
    double *temp_in;
    double *buffer;
    double *buffer2;
    double *prev;
    double *overlap;
} ChannelParams;

typedef struct AudioFWTDNContext {
    const AVClass *class;

    double sigma;
    double percent;
    double softness;

    uint64_t sn;
    int64_t eof_pts;

    int wavelet_type;
    int channels;
    int nb_samples;
    int levels;
    int wavelet_length;
    int need_profile;
    int got_profile;
    int adaptive;

    int delay;
    int drop_samples;
    int padd_samples;
    int overlap_length;
    int prev_length;
    ChannelParams *cp;

    const double *lp, *hp;
    const double *ilp, *ihp;

    AVFrame *stddev, *absmean, *filter;
    AVFrame *new_stddev, *new_absmean;

    int (*filter_channel)(AVFilterContext *ctx, void *arg, int ch, int nb_jobs);
} AudioFWTDNContext;

#define OFFSET(x) offsetof(AudioFWTDNContext, x)
#define AF AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
#define AFR AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption afwtdn_options[] = {
    { "sigma", "set noise sigma", OFFSET(sigma), AV_OPT_TYPE_DOUBLE, {.dbl=0}, 0, 1, AFR },
    { "levels", "set number of wavelet levels", OFFSET(levels), AV_OPT_TYPE_INT, {.i64=10}, 1, MAX_LEVELS-1, AF },
    { "wavet", "set wavelet type", OFFSET(wavelet_type), AV_OPT_TYPE_INT, {.i64=SYM10}, 0, NB_WAVELET_TYPES - 1, AF, "wavet" },
    { "sym2", "sym2", 0, AV_OPT_TYPE_CONST, {.i64=SYM2}, 0, 0, AF, "wavet" },
    { "sym4", "sym4", 0, AV_OPT_TYPE_CONST, {.i64=SYM4}, 0, 0, AF, "wavet" },
    { "rbior68", "rbior68", 0, AV_OPT_TYPE_CONST, {.i64=RBIOR68}, 0, 0, AF, "wavet" },
    { "deb10", "deb10", 0, AV_OPT_TYPE_CONST, {.i64=DEB10}, 0, 0, AF, "wavet" },
    { "sym10", "sym10", 0, AV_OPT_TYPE_CONST, {.i64=SYM10}, 0, 0, AF, "wavet" },
    { "coif5", "coif5", 0, AV_OPT_TYPE_CONST, {.i64=COIF5}, 0, 0, AF, "wavet" },
    { "bl3", "bl3", 0, AV_OPT_TYPE_CONST, {.i64=BL3}, 0, 0, AF, "wavet" },
    { "percent", "set percent of full denoising", OFFSET(percent),AV_OPT_TYPE_DOUBLE, {.dbl=85}, 0, 100, AFR },
    { "profile", "profile noise", OFFSET(need_profile), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, AFR },
    { "adaptive", "adaptive profiling of noise", OFFSET(adaptive), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, AFR },
    { "samples", "set frame size in number of samples", OFFSET(nb_samples), AV_OPT_TYPE_INT, {.i64=8192}, 512, 65536, AF },
    { "softness", "set thresholding softness", OFFSET(softness), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 10, AFR },
    { NULL }
};

AVFILTER_DEFINE_CLASS(afwtdn);

#define pow2(x) (1U << (x))
#define mod_pow2(x, power_of_two) ((x) & ((power_of_two) - 1))

static void conv_down(double *in, int in_length, double *low, double *high,
                      int out_length, const double *lp, const double *hp,
                      int wavelet_length, int skip,
                      double *buffer, int buffer_length)
{
    double thigh = 0.0, tlow = 0.0;
    int buff_idx = 1 + skip;

    memcpy(buffer, in, buff_idx * sizeof(*buffer));
    memset(buffer + buff_idx, 0, (buffer_length - buff_idx) * sizeof(*buffer));

    for (int i = 0; i < out_length - 1; i++) {
        double thigh = 0.0, tlow = 0.0;

        for (int j = 0; j < wavelet_length; j++) {
            const int idx = mod_pow2(-j + buff_idx - 1, buffer_length);
            const double btemp = buffer[idx];

            thigh += btemp * hp[j];
            tlow += btemp * lp[j];
        }

        high[i] = thigh;
        low[i] = tlow;
        buffer[buff_idx++] = in[2 * i + 1 + skip];
        buffer[buff_idx++] = in[2 * i + 2 + skip];
        buff_idx = mod_pow2(buff_idx, buffer_length);
    }

    for (int i = 0; i < wavelet_length; i++) {
        const int idx = mod_pow2(-i + buff_idx - 1, buffer_length);
        const double btemp = buffer[idx];

        thigh += btemp * hp[i];
        tlow += btemp * lp[i];
    }

    high[out_length - 1] = thigh;
    low[out_length - 1] = tlow;
}

static int left_ext(int wavelet_length, int levels, uint64_t sn)
{
    if (!sn)
        return 0;
    return (pow2(levels) - 1) * (wavelet_length - 2) + mod_pow2(sn, pow2(levels));
}

static int nb_coefs(int length, int level, uint64_t sn)
{
    const int pow2_level = pow2(level);

    return (sn + length) / pow2_level - sn / pow2_level;
}

static int reallocate_inputs(double **out, int *out_length,
                             int in_length, int levels, int ch, uint64_t sn)
{
    const int temp_length = nb_coefs(in_length, levels, sn);

    for (int level = 0; level < levels; level++) {
        const int temp_length = nb_coefs(in_length, level + 1, sn);

        if (temp_length > out_length[level]) {
            av_freep(&out[level]);
            out_length[level] = 0;

            out[level] = av_calloc(temp_length + 1, sizeof(**out));
            if (!out[level])
                return AVERROR(ENOMEM);
            out_length[level] = temp_length + 1;
        }

        memset(out[level] + temp_length, 0,
               (out_length[level] - temp_length) * sizeof(**out));
        out_length[level] = temp_length;
    }

    if (temp_length > out_length[levels]) {
        av_freep(&out[levels]);
        out_length[levels] = 0;

        out[levels] = av_calloc(temp_length + 1, sizeof(**out));
        if (!out[levels])
            return AVERROR(ENOMEM);
        out_length[levels] = temp_length + 1;
    }

    memset(out[levels] + temp_length, 0,
           (out_length[levels] - temp_length) * sizeof(**out));
    out_length[levels] = temp_length;

    return 0;
}

static int max_left_zeros_inverse(int levels, int level, int wavelet_length)
{
    return (pow2(levels - level) - 1) * (wavelet_length - 1);
}

static int reallocate_outputs(AudioFWTDNContext *s,
                              double **out, int *out_length,
                              int in_length, int levels, int ch, uint64_t sn)
{
    ChannelParams *cp = &s->cp[ch];
    int temp_length = 0;
    int add = 0;

    for (int level = 0; level < levels; level++) {
        temp_length = nb_coefs(in_length, level + 1, sn);
        if (temp_length > out_length[level]) {
            av_freep(&cp->subbands_to_free[level]);
            out_length[level] = 0;

            add = max_left_zeros_inverse(levels, level + 1, s->wavelet_length);
            cp->subbands_to_free[level] = av_calloc(add + temp_length + 1, sizeof(**out));
            if (!cp->subbands_to_free[level])
                return AVERROR(ENOMEM);
            out_length[level] = add + temp_length + 1;
            out[level] = cp->subbands_to_free[level] + add;
        }

        memset(out[level] + temp_length, 0,
               FFMAX(out_length[level] - temp_length - add, 0) * sizeof(**out));
        out_length[level] = temp_length;
    }

    temp_length = nb_coefs(in_length, levels, sn);
    if (temp_length > out_length[levels]) {
        av_freep(&cp->subbands_to_free[levels]);
        out_length[levels] = 0;

        cp->subbands_to_free[levels] = av_calloc(temp_length + 1, sizeof(**out));
        if (!cp->subbands_to_free[levels])
            return AVERROR(ENOMEM);
        out_length[levels] = temp_length + 1;
        out[levels] = cp->subbands_to_free[levels];
    }

    memset(out[levels] + temp_length, 0,
           (out_length[levels] - temp_length) * sizeof(**out));
    out_length[levels] = temp_length;

    return 0;
}

static int discard_left_ext(int wavelet_length, int levels, int level, uint64_t sn)
{
    if (levels == level || sn == 0)
        return 0;
    return (pow2(levels - level) - 1) * (wavelet_length - 2) + mod_pow2(sn, pow2(levels)) / pow2(level);
}

static int forward(AudioFWTDNContext *s,
                   const double *in, int in_length,
                   double **out, int *out_length, int ch, uint64_t sn)
{
    ChannelParams *cp = &s->cp[ch];
    int levels = s->levels;
    int skip = sn ? s->wavelet_length - 1 : 1;
    int leftext, ret;

    ret = reallocate_inputs(out, out_length, in_length, levels, ch, sn);
    if (ret < 0)
        return ret;
    ret = reallocate_outputs(s, cp->filter_coefs, cp->filter_length,
                             in_length, levels, ch, sn);
    if (ret < 0)
        return ret;

    leftext = left_ext(s->wavelet_length, levels, sn);

    if (cp->temp_in_max_length < in_length + cp->max_left_ext + skip) {
        av_freep(&cp->temp_in);
        cp->temp_in_max_length = in_length + cp->max_left_ext + skip;
        cp->temp_in = av_calloc(cp->temp_in_max_length, sizeof(*cp->temp_in));
        if (!cp->temp_in) {
            cp->temp_in_max_length = 0;
            return AVERROR(ENOMEM);
        }
    }

    memset(cp->temp_in, 0, cp->temp_in_max_length * sizeof(*cp->temp_in));
    cp->temp_in_length = in_length + leftext;

    if (leftext)
        memcpy(cp->temp_in, cp->prev + s->prev_length - leftext, leftext * sizeof(*cp->temp_in));
    memcpy(cp->temp_in + leftext, in, in_length * sizeof(*in));

    if (levels == 1) {
        conv_down(cp->temp_in, cp->temp_in_length, out[1], out[0], out_length[1],
                 s->lp, s->hp, s->wavelet_length, skip,
                 cp->buffer, cp->buffer_length);
    } else {
        int discard = discard_left_ext(s->wavelet_length, levels, 1, sn);
        int tempa_length_prev;

        if (cp->tempa_len_max < (in_length + cp->max_left_ext + s->wavelet_length - 1) / 2) {
            av_freep(&cp->tempa);
            av_freep(&cp->tempd);
            cp->tempa_len_max = (in_length + cp->max_left_ext + s->wavelet_length - 1) / 2;
            cp->tempa = av_calloc(cp->tempa_len_max, sizeof(*cp->tempa));
            cp->tempd = av_calloc(cp->tempa_len_max, sizeof(*cp->tempd));
            if (!cp->tempa || !cp->tempd) {
                cp->tempa_len_max = 0;
                return AVERROR(ENOMEM);
            }
        }

        memset(cp->tempa, 0, cp->tempa_len_max * sizeof(*cp->tempa));
        memset(cp->tempd, 0, cp->tempa_len_max * sizeof(*cp->tempd));

        cp->tempa_length = out_length[0] + discard;
        conv_down(cp->temp_in, cp->temp_in_length,
                 cp->tempa, cp->tempd, cp->tempa_length,
                 s->lp, s->hp, s->wavelet_length, skip,
                 cp->buffer, cp->buffer_length);
        memcpy(out[0], cp->tempd + discard, out_length[0] * sizeof(**out));
        tempa_length_prev = cp->tempa_length;

        for (int level = 1; level < levels - 1; level++) {
            if (out_length[level] == 0)
                return 0;
            discard = discard_left_ext(s->wavelet_length, levels, level + 1, sn);
            cp->tempa_length = out_length[level] + discard;
            conv_down(cp->tempa, tempa_length_prev,
                     cp->tempa, cp->tempd, cp->tempa_length,
                     s->lp, s->hp, s->wavelet_length, skip,
                     cp->buffer, cp->buffer_length);
            memcpy(out[level], cp->tempd + discard, out_length[level] * sizeof(**out));
            tempa_length_prev = cp->tempa_length;
        }

        if (out_length[levels] == 0)
            return 0;
        conv_down(cp->tempa, cp->tempa_length, out[levels], out[levels - 1], out_length[levels],
                 s->lp, s->hp, s->wavelet_length, skip,
                 cp->buffer, cp->buffer_length);
    }

    if (s->prev_length < in_length) {
        memcpy(cp->prev, in + in_length - cp->max_left_ext, cp->max_left_ext * sizeof(*cp->prev));
    } else {
        memmove(cp->prev, cp->prev + in_length, (s->prev_length - in_length) * sizeof(*cp->prev));
        memcpy(cp->prev + s->prev_length - in_length, in, in_length * sizeof(*cp->prev));
    }

    return 0;
}

static void conv_up(double *low, double *high, int in_length, double *out, int out_length,
                    const double *lp, const double *hp, int filter_length,
                    double *buffer, double *buffer2, int buffer_length)
{
    int shift = 0, buff_idx = 0, in_idx = 0;

    memset(buffer, 0, buffer_length * sizeof(*buffer));
    memset(buffer2, 0, buffer_length * sizeof(*buffer2));

    for (int i = 0; i < out_length; i++) {
        double sum = 0.0;

        if ((i & 1) == 0) {
            if (in_idx < in_length) {
                buffer[buff_idx] = low[in_idx];
                buffer2[buff_idx] = high[in_idx++];
            } else {
                buffer[buff_idx] = 0;
                buffer2[buff_idx] = 0;
            }
            buff_idx++;
            if (buff_idx >= buffer_length)
                buff_idx = 0;
            shift = 0;
        }

        for (int j = 0; j < (filter_length - shift + 1) / 2; j++) {
            const int idx = mod_pow2(-j + buff_idx - 1, buffer_length);

            sum += buffer[idx] * lp[j * 2 + shift] + buffer2[idx] * hp[j * 2 + shift];
        }
        out[i] = sum;
        shift = 1;
    }
}

static int append_left_ext(int wavelet_length, int levels, int level, uint64_t sn)
{
    if (levels == level)
        return 0;

    return (pow2(levels - level) - 1) * (wavelet_length - 2) +
            mod_pow2(sn, pow2(levels)) / pow2(level);
}

static int inverse(AudioFWTDNContext *s,
                   double **in, int *in_length,
                   double *out, int out_length, int ch, uint64_t sn)
{
    ChannelParams *cp = &s->cp[ch];
    const int levels = s->levels;
    int leftext = left_ext(s->wavelet_length, levels, sn);
    int temp_skip = 0;

    if (sn == 0)
        temp_skip = cp->min_left_ext;

    memset(out, 0, out_length * sizeof(*out));

    if (cp->temp_in_max_length < out_length + cp->max_left_ext + s->wavelet_length - 1) {
        av_freep(&cp->temp_in);
        cp->temp_in_max_length = out_length + cp->max_left_ext + s->wavelet_length - 1;
        cp->temp_in = av_calloc(cp->temp_in_max_length, sizeof(*cp->temp_in));
        if (!cp->temp_in) {
            cp->temp_in_max_length = 0;
            return AVERROR(ENOMEM);
        }
    }

    memset(cp->temp_in, 0, cp->temp_in_max_length * sizeof(*cp->temp_in));
    cp->temp_in_length = out_length + cp->max_left_ext;

    if (levels == 1) {
        conv_up(in[1], in[0], in_length[1], cp->temp_in, cp->temp_in_length,
                s->ilp, s->ihp, s->wavelet_length,
                cp->buffer, cp->buffer2, cp->buffer_length);
        memcpy(out + cp->max_left_ext - leftext, cp->temp_in + temp_skip,
               FFMAX(0, out_length - (cp->max_left_ext - leftext)) * sizeof(*out));
    } else {
        double *hp1, *hp2;
        int add, add2;

        if (cp->tempa_len_max < (out_length + cp->max_left_ext + s->wavelet_length - 1) / 2) {
            av_freep(&cp->tempa);
            cp->tempa_len_max = (out_length + cp->max_left_ext + s->wavelet_length - 1) / 2;
            cp->tempa = av_calloc(cp->tempa_len_max, sizeof(*cp->tempa));
            if (!cp->tempa) {
                cp->tempa_len_max = 0;
                return AVERROR(ENOMEM);
            }
        }

        memset(cp->tempa, 0, cp->tempa_len_max * sizeof(*cp->tempa));

        hp1 = levels & 1 ? cp->temp_in : cp->tempa;
        hp2 = levels & 1 ? cp->tempa : cp->temp_in;

        add = append_left_ext(s->wavelet_length, levels, levels - 1, sn);
        conv_up(in[levels], in[levels - 1], in_length[levels], hp1, in_length[levels - 2] + add,
               s->ilp, s->ihp, s->wavelet_length, cp->buffer, cp->buffer2, cp->buffer_length);

        for (int level = levels - 1; level > 1; level--) {
            add2 = append_left_ext(s->wavelet_length, levels, level - 1, sn);
            add = append_left_ext(s->wavelet_length, levels, level, sn);
            conv_up(hp1, in[level - 1] - add, in_length[level - 1] + add,
                    hp2, in_length[level - 2] + add2,
                    s->ilp, s->ihp, s->wavelet_length,
                    cp->buffer, cp->buffer2, cp->buffer_length);
            FFSWAP(double *, hp1, hp2);
        }

        add = append_left_ext(s->wavelet_length, levels, 1, sn);
        conv_up(hp1, in[0] - add, in_length[0] + add, cp->temp_in, cp->temp_in_length,
               s->ilp, s->ihp, s->wavelet_length,
               cp->buffer, cp->buffer2, cp->buffer_length);
    }

    memset(cp->temp_in, 0, temp_skip * sizeof(*cp->temp_in));
    if (s->overlap_length <= out_length) {
        memcpy(out + cp->max_left_ext - leftext, cp->temp_in + temp_skip,
               FFMAX(0, out_length - (cp->max_left_ext - leftext)) * sizeof(*out));
        for (int i = 0;i < FFMIN(s->overlap_length, out_length); i++)
            out[i] += cp->overlap[i];

        memcpy(cp->overlap, cp->temp_in + out_length - (cp->max_left_ext - leftext),
               s->overlap_length * sizeof(*cp->overlap));
    } else {
        for (int i = 0;i < s->overlap_length - (cp->max_left_ext - leftext); i++)
            cp->overlap[i + cp->max_left_ext - leftext] += cp->temp_in[i];
        memcpy(out, cp->overlap, out_length * sizeof(*out));
        memmove(cp->overlap, cp->overlap + out_length,
                (s->overlap_length - out_length) * sizeof(*cp->overlap));
        memcpy(cp->overlap + s->overlap_length - out_length, cp->temp_in + leftext,
               out_length * sizeof(*cp->overlap));
    }

    return 0;
}

static int next_pow2(int in)
{
    return 1 << (av_log2(in) + 1);
}

static void denoise_level(double *out, const double *in,
                          const double *filter,
                          double percent, int length)
{
    const double x = percent * 0.01;
    const double y = 1.0 - x;

    for (int i = 0; i < length; i++)
        out[i] = x * filter[i] + in[i] * y;
}

static double sqr(double in)
{
    return in * in;
}

static double measure_mean(const double *in, int length)
{
    double sum = 0.0;

    for (int i = 0; i < length; i++)
        sum += in[i];

    return sum / length;
}

static double measure_absmean(const double *in, int length)
{
    double sum = 0.0;

    for (int i = 0; i < length; i++)
        sum += fabs(in[i]);

    return sum / length;
}

static double measure_stddev(const double *in, int length, double mean)
{
    double sum = 0.;

    for (int i = 0; i < length; i++) {
        sum += sqr(in[i] - mean);
    }

    return sqrt(sum / length);
}

static void noise_filter(const double stddev, const double *in,
                         double *out, double absmean, double softness,
                         double new_stddev, int length)
{
    for (int i = 0; i < length; i++) {
        if (new_stddev <= stddev)
            out[i] = 0.0;
        else if (fabs(in[i]) <= absmean)
            out[i] = 0.0;
        else
            out[i] = in[i] - FFSIGN(in[i]) * absmean / exp(3.0 * softness * (fabs(in[i]) - absmean) / absmean);
    }
}

typedef struct ThreadData {
    AVFrame *in, *out;
} ThreadData;

static int filter_channel(AVFilterContext *ctx, void *arg, int ch, int nb_jobs)
{
    AudioFWTDNContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *in = td->in;
    AVFrame *out = td->out;
    ChannelParams *cp = &s->cp[ch];
    const double *src = (const double *)(in->extended_data[ch]);
    double *dst = (double *)out->extended_data[ch];
    double *absmean = (double *)s->absmean->extended_data[ch];
    double *new_absmean = (double *)s->new_absmean->extended_data[ch];
    double *stddev = (double *)s->stddev->extended_data[ch];
    double *new_stddev = (double *)s->new_stddev->extended_data[ch];
    double *filter = (double *)s->filter->extended_data[ch];
    double is_noise = 0.0;
    int ret;

    ret = forward(s, src, in->nb_samples, cp->output_coefs, cp->output_length, ch, s->sn);
    if (ret < 0)
        return ret;

    if (!s->got_profile && s->need_profile) {
        for (int level = 0; level <= s->levels; level++) {
            const int length = cp->output_length[level];
            const double scale = sqrt(2.0 * log(length));

            stddev[level] = measure_stddev(cp->output_coefs[level], length,
                            measure_mean(cp->output_coefs[level], length)) * scale;
            absmean[level] = measure_absmean(cp->output_coefs[level], length) * scale;
        }
    } else if (!s->got_profile && !s->need_profile && !s->adaptive) {
        for (int level = 0; level <= s->levels; level++) {
            const int length = cp->output_length[level];
            const double scale = sqrt(2.0 * log(length));

            stddev[level] = 0.5 * s->sigma * scale;
            absmean[level] = 0.5 * s->sigma * scale;
        }
    }

    for (int level = 0; level <= s->levels; level++) {
        const int length = cp->output_length[level];
        double vad;

        new_stddev[level] = measure_stddev(cp->output_coefs[level], length,
                            measure_mean(cp->output_coefs[level], length));
        new_absmean[level] = measure_absmean(cp->output_coefs[level], length);
        if (new_absmean[level] <= FLT_EPSILON)
            vad = 1.0;
        else
            vad = new_stddev[level] / new_absmean[level];
        if (level < s->levels)
            is_noise += sqr(vad - 1.232);
    }

    is_noise *= in->sample_rate;
    is_noise /= s->nb_samples;
    for (int level = 0; level <= s->levels; level++) {
        const double percent = ctx->is_disabled ? 0. : s->percent;
        const int length = cp->output_length[level];
        const double scale = sqrt(2.0 * log(length));

        if (is_noise < 0.05 && s->adaptive) {
            stddev[level] = new_stddev[level] * scale;
            absmean[level] = new_absmean[level] * scale;
        }

        noise_filter(stddev[level], cp->output_coefs[level], filter, absmean[level],
                     s->softness, new_stddev[level], length);
        denoise_level(cp->filter_coefs[level], cp->output_coefs[level], filter, percent, length);
    }

    ret = inverse(s, cp->filter_coefs, cp->filter_length, dst, out->nb_samples, ch, s->sn);
    if (ret < 0)
        return ret;

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AudioFWTDNContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    ThreadData td;
    AVFrame *out;
    int eof = in == NULL;

    out = ff_get_audio_buffer(outlink, s->nb_samples);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    if (in) {
        av_frame_copy_props(out, in);
        s->eof_pts = in->pts + in->nb_samples;
    }
    if (eof)
        out->pts = s->eof_pts - s->padd_samples;

    if (!in || in->nb_samples < s->nb_samples) {
        AVFrame *new_in = ff_get_audio_buffer(outlink, s->nb_samples);

        if (!new_in) {
            av_frame_free(&in);
            av_frame_free(&out);
            return AVERROR(ENOMEM);
        }
        if (in)
            av_frame_copy_props(new_in, in);

        s->padd_samples -= s->nb_samples - (in ? in->nb_samples: 0);
        if (in)
            av_samples_copy(new_in->extended_data, in->extended_data, 0, 0,
                            in->nb_samples, in->ch_layout.nb_channels, in->format);
        av_frame_free(&in);
        in = new_in;
    }

    td.in  = in;
    td.out = out;
    ff_filter_execute(ctx, s->filter_channel, &td, NULL, inlink->ch_layout.nb_channels);
    if (s->need_profile)
        s->got_profile = 1;

    s->sn += s->nb_samples;

    if (s->drop_samples >= in->nb_samples) {
        s->drop_samples -= in->nb_samples;
        s->delay += in->nb_samples;
        av_frame_free(&in);
        av_frame_free(&out);
        FF_FILTER_FORWARD_STATUS(inlink, outlink);
        FF_FILTER_FORWARD_WANTED(outlink, inlink);
        return 0;
    } else if (s->drop_samples > 0) {
        for (int ch = 0; ch < out->ch_layout.nb_channels; ch++) {
            memmove(out->extended_data[ch],
                    out->extended_data[ch] + s->drop_samples * sizeof(double),
                    (in->nb_samples - s->drop_samples) * sizeof(double));
        }

        out->nb_samples = in->nb_samples - s->drop_samples;
        out->pts = in->pts - av_rescale_q(s->delay, (AVRational){1, outlink->sample_rate}, outlink->time_base);
        s->delay += s->drop_samples;
        s->drop_samples = 0;
    } else {
        if (s->padd_samples < 0 && eof) {
            out->nb_samples += s->padd_samples;
            s->padd_samples = 0;
        }
        if (!eof)
            out->pts = in->pts - av_rescale_q(s->delay, (AVRational){1, outlink->sample_rate}, outlink->time_base);
    }

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static int max_left_ext(int wavelet_length, int levels)
{
    return (pow2(levels) - 1) * (wavelet_length - 1);
}

static int min_left_ext(int wavelet_length, int levels)
{
    return (pow2(levels) - 1) * (wavelet_length - 2);
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AudioFWTDNContext *s = ctx->priv;

    switch (s->wavelet_type) {
    case SYM2:
        s->wavelet_length = 4;
        s->lp  = sym2_lp;
        s->hp  = sym2_hp;
        s->ilp = sym2_ilp;
        s->ihp = sym2_ihp;
        break;
    case SYM4:
        s->wavelet_length = 8;
        s->lp  = sym4_lp;
        s->hp  = sym4_hp;
        s->ilp = sym4_ilp;
        s->ihp = sym4_ihp;
        break;
    case RBIOR68:
        s->wavelet_length = 18;
        s->lp  = rbior68_lp;
        s->hp  = rbior68_hp;
        s->ilp = rbior68_ilp;
        s->ihp = rbior68_ihp;
        break;
    case DEB10:
        s->wavelet_length = 20;
        s->lp  = deb10_lp;
        s->hp  = deb10_hp;
        s->ilp = deb10_ilp;
        s->ihp = deb10_ihp;
        break;
    case SYM10:
        s->wavelet_length = 20;
        s->lp  = sym10_lp;
        s->hp  = sym10_hp;
        s->ilp = sym10_ilp;
        s->ihp = sym10_ihp;
        break;
    case COIF5:
        s->wavelet_length = 30;
        s->lp  = coif5_lp;
        s->hp  = coif5_hp;
        s->ilp = coif5_ilp;
        s->ihp = coif5_ihp;
        break;
    case BL3:
        s->wavelet_length = 42;
        s->lp  = bl3_lp;
        s->hp  = bl3_hp;
        s->ilp = bl3_ilp;
        s->ihp = bl3_ihp;
        break;
    default:
        av_assert0(0);
    }

    s->levels = FFMIN(s->levels, lrint(log(s->nb_samples / (s->wavelet_length - 1.0)) / M_LN2));
    av_log(ctx, AV_LOG_VERBOSE, "levels: %d\n", s->levels);
    s->filter_channel = filter_channel;

    s->stddev = ff_get_audio_buffer(outlink, MAX_LEVELS);
    s->new_stddev = ff_get_audio_buffer(outlink, MAX_LEVELS);
    s->filter = ff_get_audio_buffer(outlink, s->nb_samples);
    s->absmean = ff_get_audio_buffer(outlink, MAX_LEVELS);
    s->new_absmean = ff_get_audio_buffer(outlink, MAX_LEVELS);
    if (!s->stddev || !s->absmean || !s->filter ||
        !s->new_stddev || !s->new_absmean)
        return AVERROR(ENOMEM);

    s->channels = outlink->ch_layout.nb_channels;
    s->overlap_length = max_left_ext(s->wavelet_length, s->levels);
    s->prev_length = s->overlap_length;
    s->drop_samples = s->overlap_length;
    s->padd_samples = s->overlap_length;
    s->sn = 1;

    s->cp = av_calloc(s->channels, sizeof(*s->cp));
    if (!s->cp)
        return AVERROR(ENOMEM);

    for (int ch = 0; ch < s->channels; ch++) {
        ChannelParams *cp = &s->cp[ch];

        cp->output_coefs = av_calloc(s->levels + 1, sizeof(*cp->output_coefs));
        cp->filter_coefs = av_calloc(s->levels + 1, sizeof(*cp->filter_coefs));
        cp->output_length = av_calloc(s->levels + 1, sizeof(*cp->output_length));
        cp->filter_length = av_calloc(s->levels + 1, sizeof(*cp->filter_length));
        cp->buffer_length = next_pow2(s->wavelet_length);
        cp->buffer = av_calloc(cp->buffer_length, sizeof(*cp->buffer));
        cp->buffer2 = av_calloc(cp->buffer_length, sizeof(*cp->buffer2));
        cp->subbands_to_free = av_calloc(s->levels + 1, sizeof(*cp->subbands_to_free));
        cp->prev = av_calloc(s->prev_length, sizeof(*cp->prev));
        cp->overlap = av_calloc(s->overlap_length, sizeof(*cp->overlap));
        cp->max_left_ext = max_left_ext(s->wavelet_length, s->levels);
        cp->min_left_ext = min_left_ext(s->wavelet_length, s->levels);
        if (!cp->output_coefs || !cp->filter_coefs || !cp->output_length ||
            !cp->filter_length || !cp->subbands_to_free || !cp->prev || !cp->overlap ||
            !cp->buffer || !cp->buffer2)
            return AVERROR(ENOMEM);
    }

    return 0;
}

static int activate(AVFilterContext *ctx)
{
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    AudioFWTDNContext *s = ctx->priv;
    AVFrame *in = NULL;
    int ret, status;
    int64_t pts;

    FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink);

    ret = ff_inlink_consume_samples(inlink, s->nb_samples, s->nb_samples, &in);
    if (ret < 0)
        return ret;
    if (ret > 0)
        return filter_frame(inlink, in);

    if (ff_inlink_acknowledge_status(inlink, &status, &pts)) {
        if (status == AVERROR_EOF) {
            while (s->padd_samples != 0) {
                ret = filter_frame(inlink, NULL);
                if (ret < 0)
                    return ret;
            }
            ff_outlink_set_status(outlink, status, pts);
            return ret;
        }
    }
    FF_FILTER_FORWARD_WANTED(outlink, inlink);

    return FFERROR_NOT_READY;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AudioFWTDNContext *s = ctx->priv;

    av_frame_free(&s->filter);
    av_frame_free(&s->new_stddev);
    av_frame_free(&s->stddev);
    av_frame_free(&s->new_absmean);
    av_frame_free(&s->absmean);

    for (int ch = 0; s->cp && ch < s->channels; ch++) {
        ChannelParams *cp = &s->cp[ch];

        av_freep(&cp->tempa);
        av_freep(&cp->tempd);
        av_freep(&cp->temp_in);
        av_freep(&cp->buffer);
        av_freep(&cp->buffer2);
        av_freep(&cp->prev);
        av_freep(&cp->overlap);

        av_freep(&cp->output_length);
        av_freep(&cp->filter_length);

        if (cp->output_coefs) {
            for (int level = 0; level <= s->levels; level++)
                av_freep(&cp->output_coefs[level]);
        }

        if (cp->subbands_to_free) {
            for (int level = 0; level <= s->levels; level++)
                av_freep(&cp->subbands_to_free[level]);
        }

        av_freep(&cp->subbands_to_free);
        av_freep(&cp->output_coefs);
        av_freep(&cp->filter_coefs);
    }

    av_freep(&s->cp);
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    AudioFWTDNContext *s = ctx->priv;
    int ret;

    ret = ff_filter_process_command(ctx, cmd, args, res, res_len, flags);
    if (ret < 0)
        return ret;

    if (!strcmp(cmd, "profile") && s->need_profile)
        s->got_profile = 0;

    return 0;
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
    },
};

static const AVFilterPad outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .config_props  = config_output,
    },
};

const AVFilter ff_af_afwtdn = {
    .name            = "afwtdn",
    .description     = NULL_IF_CONFIG_SMALL("Denoise audio stream using Wavelets."),
    .priv_size       = sizeof(AudioFWTDNContext),
    .priv_class      = &afwtdn_class,
    .activate        = activate,
    .uninit          = uninit,
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(outputs),
    FILTER_SINGLE_SAMPLEFMT(AV_SAMPLE_FMT_DBLP),
    .process_command = process_command,
    .flags           = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL |
                       AVFILTER_FLAG_SLICE_THREADS,
};
