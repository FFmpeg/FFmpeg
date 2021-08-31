/*
 * Copyright (c) 1998 - 2009 Conifer Software
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

/**
 * @file
 * ReplayGain scanner
 */

#include "libavutil/avassert.h"
#include "libavutil/channel_layout.h"
#include "audio.h"
#include "avfilter.h"
#include "internal.h"

#define HISTOGRAM_SLOTS 12000
#define BUTTER_ORDER        2
#define YULE_ORDER         10

typedef struct ReplayGainFreqInfo {
    int    sample_rate;
    double BYule[YULE_ORDER + 1];
    double AYule[YULE_ORDER + 1];
    double BButter[BUTTER_ORDER + 1];
    double AButter[BUTTER_ORDER + 1];
} ReplayGainFreqInfo;

static const ReplayGainFreqInfo freqinfos[] =
{
    {
        192000,
        {  0.01184742123123, -0.04631092400086,  0.06584226961238,
          -0.02165588522478, -0.05656260778952,  0.08607493592760,
          -0.03375544339786, -0.04216579932754,  0.06416711490648,
          -0.03444708260844,  0.00697275872241 },
        {  1.00000000000000, -5.24727318348167, 10.60821585192244,
          -8.74127665810413, -1.33906071371683,  8.07972882096606,
          -5.46179918950847,  0.54318070652536,  0.87450969224280,
          -0.34656083539754,  0.03034796843589 },
        {  0.99653501465135, -1.99307002930271,  0.99653501465135 },
        {  1.00000000000000, -1.99305802314321,  0.99308203546221 },
    },
    {
        176400,
        {  0.00268568524529, -0.00852379426080,  0.00852704191347,
           0.00146116310295, -0.00950855828762,  0.00625449515499,
           0.00116183868722, -0.00362461417136,  0.00203961000134,
          -0.00050664587933,  0.00004327455427 },
        {  1.00000000000000, -5.57512782763045, 12.44291056065794,
         -12.87462799681221,  3.08554846961576,  6.62493459880692,
          -7.07662766313248,  2.51175542736441,  0.06731510802735,
          -0.24567753819213,  0.03961404162376 },
        {  0.99622916581118, -1.99245833162236,  0.99622916581118 },
        {  1.00000000000000, -1.99244411238133,  0.99247255086339 },
    },
    {
        144000,
        {  0.00639682359450, -0.02556437970955,  0.04230854400938,
          -0.03722462201267,  0.01718514827295,  0.00610592243009,
          -0.03065965747365,  0.04345745003539, -0.03298592681309,
           0.01320937236809, -0.00220304127757 },
        {  1.00000000000000, -6.14814623523425, 15.80002457141566,
         -20.78487587686937, 11.98848552310315,  3.36462015062606,
         -10.22419868359470,  6.65599702146473, -1.67141861110485,
          -0.05417956536718,  0.07374767867406 },
        {  0.99538268958706, -1.99076537917413,  0.99538268958706 },
        {  1.00000000000000, -1.99074405950505,  0.99078669884321 },
    },
    {
        128000,
        {  0.00553120584305, -0.02112620545016,  0.03549076243117,
          -0.03362498312306,  0.01425867248183,  0.01344686928787,
          -0.03392770787836,  0.03464136459530, -0.02039116051549,
           0.00667420794705, -0.00093763762995 },
        {  1.00000000000000, -6.14581710839925, 16.04785903675838,
         -22.19089131407749, 15.24756471580286, -0.52001440400238,
          -8.00488641699940,  6.60916094768855, -2.37856022810923,
           0.33106947986101,  0.00459820832036 },
        {  0.99480702681278, -1.98961405362557,  0.99480702681278 },
        {  1.00000000000000, -1.98958708647324,  0.98964102077790 },
    },
    {
        112000,
        {  0.00528778718259, -0.01893240907245,  0.03185982561867,
          -0.02926260297838,  0.00715743034072,  0.01985743355827,
          -0.03222614850941,  0.02565681978192, -0.01210662313473,
           0.00325436284541, -0.00044173593001 },
        {  1.00000000000000, -6.24932108456288, 17.42344320538476,
         -27.86819709054896, 26.79087344681326,-13.43711081485123,
          -0.66023612948173,  6.03658091814935, -4.24926577030310,
           1.40829268709186, -0.19480852628112 },
        {  0.99406737810867, -1.98813475621734,  0.99406737810867 },
        {  1.00000000000000, -1.98809955990514,  0.98816995252954 },
    },
    {
        96000,
        {  0.00588138296683, -0.01613559730421,  0.02184798954216,
          -0.01742490405317,  0.00464635643780,  0.01117772513205,
          -0.02123865824368,  0.01959354413350, -0.01079720643523,
           0.00352183686289, -0.00063124341421 },
        {  1.00000000000000, -5.97808823642008, 16.21362507964068,
         -25.72923730652599, 25.40470663139513,-14.66166287771134,
           2.81597484359752,  2.51447125969733, -2.23575306985286,
           0.75788151036791, -0.10078025199029 },
        {  0.99308203517541, -1.98616407035082,  0.99308203517541 },
        {  1.00000000000000, -1.98611621154089,  0.98621192916075 },
    },
    {
        88200,
        {  0.02667482047416, -0.11377479336097,  0.23063167910965,
          -0.30726477945593,  0.33188520686529, -0.33862680249063,
           0.31807161531340, -0.23730796929880,  0.12273894790371,
          -0.03840017967282,  0.00549673387936 },
        {  1.00000000000000, -6.31836451657302, 18.31351310801799,
         -31.88210014815921, 36.53792146976740,-28.23393036467559,
          14.24725258227189, -4.04670980012854,  0.18865757280515,
           0.25420333563908, -0.06012333531065 },
        {  0.99247255046129, -1.98494510092259,  0.99247255046129 },
        {  1.00000000000000, -1.98488843762335,  0.98500176422183 },
    },
    {
        64000,
        {  0.02613056568174, -0.08128786488109,  0.14937282347325,
          -0.21695711675126,  0.25010286673402, -0.23162283619278,
           0.17424041833052, -0.10299599216680,  0.04258696481981,
          -0.00977952936493,  0.00105325558889 },
        {  1.00000000000000, -5.73625477092119, 16.15249794355035,
         -29.68654912464508, 39.55706155674083,-39.82524556246253,
          30.50605345013009,-17.43051772821245,  7.05154573908017,
          -1.80783839720514,  0.22127840210813 },
        {  0.98964101933472, -1.97928203866944,  0.98964101933472 },
        {  1.00000000000000, -1.97917472731009,  0.97938935002880 },
    },
    {
        56000,
        {  0.03144914734085, -0.06151729206963,  0.08066788708145,
          -0.09737939921516,  0.08943210803999, -0.06989984672010,
           0.04926972841044, -0.03161257848451,  0.01456837493506,
          -0.00316015108496,  0.00132807215875 },
        {  1.00000000000000, -4.87377313090032, 12.03922160140209,
         -20.10151118381395, 25.10388534415171,-24.29065560815903,
          18.27158469090663,-10.45249552560593,  4.30319491872003,
          -1.13716992070185,  0.14510733527035 },
        {  0.98816995007392, -1.97633990014784,  0.98816995007392 },
        {  1.00000000000000, -1.97619994516973,  0.97647985512594 },
    },
    {
        48000,
        {  0.03857599435200, -0.02160367184185, -0.00123395316851,
          -0.00009291677959, -0.01655260341619,  0.02161526843274,
          -0.02074045215285,  0.00594298065125,  0.00306428023191,
           0.00012025322027,  0.00288463683916 },
        {  1.00000000000000, -3.84664617118067,  7.81501653005538,
         -11.34170355132042, 13.05504219327545,-12.28759895145294,
           9.48293806319790, -5.87257861775999,  2.75465861874613,
          -0.86984376593551,  0.13919314567432 },
        {  0.98621192462708, -1.97242384925416,  0.98621192462708 },
        {  1.00000000000000, -1.97223372919527,  0.97261396931306 },
    },
    {
        44100,
        {  0.05418656406430, -0.02911007808948, -0.00848709379851,
          -0.00851165645469, -0.00834990904936,  0.02245293253339,
          -0.02596338512915,  0.01624864962975, -0.00240879051584,
           0.00674613682247, -0.00187763777362 },
        {  1.00000000000000, -3.47845948550071,  6.36317777566148,
          -8.54751527471874,  9.47693607801280, -8.81498681370155,
           6.85401540936998, -4.39470996079559,  2.19611684890774,
          -0.75104302451432,  0.13149317958808 },
        {  0.98500175787242, -1.97000351574484,  0.98500175787242 },
        {  1.00000000000000, -1.96977855582618,  0.97022847566350 },
    },
    {
        37800,
        {  0.08717879977844, -0.01000374016172, -0.06265852122368,
          -0.01119328800950, -0.00114279372960,  0.02081333954769,
          -0.01603261863207,  0.01936763028546,  0.00760044736442,
          -0.00303979112271, -0.00075088605788 },
        {  1.00000000000000, -2.62816311472146,  3.53734535817992,
          -3.81003448678921,  3.91291636730132, -3.53518605896288,
           2.71356866157873, -1.86723311846592,  1.12075382367659,
          -0.48574086886890,  0.11330544663849 },
        {  0.98252400815195, -1.96504801630391,  0.98252400815195 },
        {  1.00000000000000, -1.96474258269041,  0.96535344991740 },
    },
    {
        32000,
        {  0.15457299681924, -0.09331049056315, -0.06247880153653,
           0.02163541888798, -0.05588393329856,  0.04781476674921,
           0.00222312597743,  0.03174092540049, -0.01390589421898,
           0.00651420667831, -0.00881362733839 },
        {  1.00000000000000, -2.37898834973084,  2.84868151156327,
          -2.64577170229825,  2.23697657451713, -1.67148153367602,
           1.00595954808547, -0.45953458054983,  0.16378164858596,
          -0.05032077717131,  0.02347897407020 },
        {  0.97938932735214, -1.95877865470428,  0.97938932735214 },
        {  1.00000000000000, -1.95835380975398,  0.95920349965459 },
    },
    {
        24000,
        {  0.30296907319327, -0.22613988682123, -0.08587323730772,
           0.03282930172664, -0.00915702933434, -0.02364141202522,
          -0.00584456039913,  0.06276101321749, -0.00000828086748,
           0.00205861885564, -0.02950134983287 },
        {  1.00000000000000, -1.61273165137247,  1.07977492259970,
          -0.25656257754070, -0.16276719120440, -0.22638893773906,
           0.39120800788284, -0.22138138954925,  0.04500235387352,
           0.02005851806501,  0.00302439095741 },
        {  0.97531843204928, -1.95063686409857,  0.97531843204928 },
        {  1.00000000000000, -1.95002759149878,  0.95124613669835 },
    },
    {
        22050,
        {  0.33642304856132, -0.25572241425570, -0.11828570177555,
           0.11921148675203, -0.07834489609479, -0.00469977914380,
          -0.00589500224440,  0.05724228140351,  0.00832043980773,
          -0.01635381384540, -0.01760176568150 },
        {  1.00000000000000, -1.49858979367799,  0.87350271418188,
           0.12205022308084, -0.80774944671438,  0.47854794562326,
          -0.12453458140019, -0.04067510197014,  0.08333755284107,
          -0.04237348025746,  0.02977207319925 },
        {  0.97316523498161, -1.94633046996323,  0.97316523498161 },
        {  1.00000000000000, -1.94561023566527,  0.94705070426118 },
    },
    {
        18900,
        {  0.38524531015142, -0.27682212062067, -0.09980181488805,
           0.09951486755646, -0.08934020156622, -0.00322369330199,
          -0.00110329090689,  0.03784509844682,  0.01683906213303,
          -0.01147039862572, -0.01941767987192 },
        {  1.00000000000000, -1.29708918404534,  0.90399339674203,
          -0.29613799017877, -0.42326645916207,  0.37934887402200,
          -0.37919795944938,  0.23410283284785, -0.03892971758879,
           0.00403009552351,  0.03640166626278 },
        {  0.96535326815829, -1.93070653631658,  0.96535326815829 },
        {  1.00000000000000, -1.92950577983524,  0.93190729279793 },
    },
    {
        16000,
        {  0.44915256608450, -0.14351757464547, -0.22784394429749,
          -0.01419140100551,  0.04078262797139, -0.12398163381748,
           0.04097565135648,  0.10478503600251, -0.01863887810927,
          -0.03193428438915,  0.00541907748707 },
        {  1.00000000000000, -0.62820619233671,  0.29661783706366,
          -0.37256372942400,  0.00213767857124, -0.42029820170918,
           0.22199650564824,  0.00613424350682,  0.06747620744683,
           0.05784820375801,  0.03222754072173 },
        {  0.96454515552826, -1.92909031105652,  0.96454515552826 },
        {  1.00000000000000, -1.92783286977036,  0.93034775234268 },
    },
    {
        12000,
        {  0.56619470757641, -0.75464456939302,  0.16242137742230,
           0.16744243493672, -0.18901604199609,  0.30931782841830,
          -0.27562961986224,  0.00647310677246,  0.08647503780351,
          -0.03788984554840, -0.00588215443421 },
        {  1.00000000000000, -1.04800335126349,  0.29156311971249,
          -0.26806001042947,  0.00819999645858,  0.45054734505008,
          -0.33032403314006,  0.06739368333110, -0.04784254229033,
           0.01639907836189,  0.01807364323573 },
        {  0.96009142950541, -1.92018285901082,  0.96009142950541 },
        {  1.00000000000000, -1.91858953033784,  0.92177618768381 },
    },
    {
        11025,
        {  0.58100494960553, -0.53174909058578, -0.14289799034253,
           0.17520704835522,  0.02377945217615,  0.15558449135573,
          -0.25344790059353,  0.01628462406333,  0.06920467763959,
          -0.03721611395801, -0.00749618797172 },
        {  1.00000000000000, -0.51035327095184, -0.31863563325245,
          -0.20256413484477,  0.14728154134330,  0.38952639978999,
          -0.23313271880868, -0.05246019024463, -0.02505961724053,
           0.02442357316099,  0.01818801111503 },
        {  0.95856916599601, -1.91713833199203,  0.95856916599601 },
        {  1.00000000000000, -1.91542108074780,  0.91885558323625 },
    },
    {
        8000,
        {  0.53648789255105, -0.42163034350696, -0.00275953611929,
           0.04267842219415, -0.10214864179676,  0.14590772289388,
          -0.02459864859345, -0.11202315195388, -0.04060034127000,
           0.04788665548180, -0.02217936801134 },
        {  1.00000000000000, -0.25049871956020, -0.43193942311114,
          -0.03424681017675, -0.04678328784242,  0.26408300200955,
           0.15113130533216, -0.17556493366449, -0.18823009262115,
           0.05477720428674,  0.04704409688120 },
        {  0.94597685600279, -1.89195371200558,  0.94597685600279 },
        {  1.00000000000000, -1.88903307939452,  0.89487434461664 },
    },
};

typedef struct ReplayGainContext {
    uint32_t histogram[HISTOGRAM_SLOTS];
    float peak;
    int yule_hist_i, butter_hist_i;
    const double *yule_coeff_a;
    const double *yule_coeff_b;
    const double *butter_coeff_a;
    const double *butter_coeff_b;
    float yule_hist_a[256];
    float yule_hist_b[256];
    float butter_hist_a[256];
    float butter_hist_b[256];
} ReplayGainContext;

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats = NULL;
    AVFilterChannelLayouts *layout = NULL;
    int i, ret;

    if ((ret = ff_add_format                 (&formats, AV_SAMPLE_FMT_FLT  )) < 0 ||
        (ret = ff_set_common_formats         (ctx     , formats            )) < 0 ||
        (ret = ff_add_channel_layout         (&layout , &(AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO)) < 0 ||
        (ret = ff_set_common_channel_layouts (ctx     , layout             )) < 0)
        return ret;

    formats = NULL;
    for (i = 0; i < FF_ARRAY_ELEMS(freqinfos); i++) {
        if ((ret = ff_add_format(&formats, freqinfos[i].sample_rate)) < 0)
            return ret;
    }

    return ff_set_common_samplerates(ctx, formats);
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    ReplayGainContext *s = ctx->priv;
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(freqinfos); i++) {
        if (freqinfos[i].sample_rate == inlink->sample_rate)
            break;
    }
    av_assert0(i < FF_ARRAY_ELEMS(freqinfos));

    s->yule_coeff_a   = freqinfos[i].AYule;
    s->yule_coeff_b   = freqinfos[i].BYule;
    s->butter_coeff_a = freqinfos[i].AButter;
    s->butter_coeff_b = freqinfos[i].BButter;

    s->yule_hist_i   = 20;
    s->butter_hist_i = 4;
    inlink->min_samples =
    inlink->max_samples = inlink->sample_rate / 20;

    return 0;
}

/*
 * Update largest absolute sample value.
 */
static void calc_stereo_peak(const float *samples, int nb_samples,
                             float *peak_p)
{
    float peak = 0.0;

    while (nb_samples--) {
        if (samples[0] > peak)
            peak = samples[0];
        else if (-samples[0] > peak)
            peak = -samples[0];

        if (samples[1] > peak)
            peak = samples[1];
        else if (-samples[1] > peak)
            peak = -samples[1];

        samples += 2;
    }

    *peak_p = FFMAX(peak, *peak_p);
}

/*
 * Calculate stereo RMS level. Minimum value is about -100 dB for
 * digital silence. The 90 dB offset is to compensate for the
 * normalized float range and 3 dB is for stereo samples.
 */
static double calc_stereo_rms(const float *samples, int nb_samples)
{
    int count = nb_samples;
    double sum = 1e-16;

    while (count--) {
        sum += samples[0] * samples[0] + samples[1] * samples[1];
        samples += 2;
    }

    return 10 * log10 (sum / nb_samples) + 90.0 - 3.0;
}

/*
 * Optimized implementation of 2nd-order IIR stereo filter.
 */
static void butter_filter_stereo_samples(ReplayGainContext *s,
                                         float *samples, int nb_samples)
{
    const double *coeff_a = s->butter_coeff_a;
    const double *coeff_b = s->butter_coeff_b;
    float *hist_a   = s->butter_hist_a;
    float *hist_b   = s->butter_hist_b;
    double left, right;
    int i, j;

    i = s->butter_hist_i;

    // If filter history is very small magnitude, clear it completely
    // to prevent denormals from rattling around in there forever
    // (slowing us down).

    for (j = -4; j < 0; ++j)
        if (fabsf(hist_a[i + j]) > 1e-10f || fabsf(hist_b[i + j]) > 1e-10f)
            break;

    if (!j) {
        memset(s->butter_hist_a, 0, sizeof(s->butter_hist_a));
        memset(s->butter_hist_b, 0, sizeof(s->butter_hist_b));
    }

    while (nb_samples--) {
        left   = (hist_b[i    ] = samples[0]) * coeff_b[0];
        right  = (hist_b[i + 1] = samples[1]) * coeff_b[0];
        left  += hist_b[i - 2] * coeff_b[1] - hist_a[i - 2] * coeff_a[1];
        right += hist_b[i - 1] * coeff_b[1] - hist_a[i - 1] * coeff_a[1];
        left  += hist_b[i - 4] * coeff_b[2] - hist_a[i - 4] * coeff_a[2];
        right += hist_b[i - 3] * coeff_b[2] - hist_a[i - 3] * coeff_a[2];
        samples[0] = hist_a[i    ] = (float) left;
        samples[1] = hist_a[i + 1] = (float) right;
        samples += 2;

        if ((i += 2) == 256) {
            memcpy(hist_a, hist_a + 252, sizeof(*hist_a) * 4);
            memcpy(hist_b, hist_b + 252, sizeof(*hist_b) * 4);
            i = 4;
        }
    }

    s->butter_hist_i = i;
}

/*
 * Optimized implementation of 10th-order IIR stereo filter.
 */
static void yule_filter_stereo_samples(ReplayGainContext *s, const float *src,
                                       float *dst, int nb_samples)
{
    const double *coeff_a = s->yule_coeff_a;
    const double *coeff_b = s->yule_coeff_b;
    float *hist_a   = s->yule_hist_a;
    float *hist_b   = s->yule_hist_b;
    double left, right;
    int i, j;

    i = s->yule_hist_i;

    // If filter history is very small magnitude, clear it completely to
    // prevent denormals from rattling around in there forever
    // (slowing us down).

    for (j = -20; j < 0; ++j)
        if (fabsf(hist_a[i + j]) > 1e-10f || fabsf(hist_b[i + j]) > 1e-10f)
            break;

    if (!j) {
        memset(s->yule_hist_a, 0, sizeof(s->yule_hist_a));
        memset(s->yule_hist_b, 0, sizeof(s->yule_hist_b));
    }

    while (nb_samples--) {
        left   = (hist_b[i] = src[0]) * coeff_b[0];
        right  = (hist_b[i + 1] = src[1]) * coeff_b[0];
        left  += hist_b[i -  2] * coeff_b[ 1] - hist_a[i -  2] * coeff_a[1 ];
        right += hist_b[i -  1] * coeff_b[ 1] - hist_a[i -  1] * coeff_a[1 ];
        left  += hist_b[i -  4] * coeff_b[ 2] - hist_a[i -  4] * coeff_a[2 ];
        right += hist_b[i -  3] * coeff_b[ 2] - hist_a[i -  3] * coeff_a[2 ];
        left  += hist_b[i -  6] * coeff_b[ 3] - hist_a[i -  6] * coeff_a[3 ];
        right += hist_b[i -  5] * coeff_b[ 3] - hist_a[i -  5] * coeff_a[3 ];
        left  += hist_b[i -  8] * coeff_b[ 4] - hist_a[i -  8] * coeff_a[4 ];
        right += hist_b[i -  7] * coeff_b[ 4] - hist_a[i -  7] * coeff_a[4 ];
        left  += hist_b[i - 10] * coeff_b[ 5] - hist_a[i - 10] * coeff_a[5 ];
        right += hist_b[i -  9] * coeff_b[ 5] - hist_a[i -  9] * coeff_a[5 ];
        left  += hist_b[i - 12] * coeff_b[ 6] - hist_a[i - 12] * coeff_a[6 ];
        right += hist_b[i - 11] * coeff_b[ 6] - hist_a[i - 11] * coeff_a[6 ];
        left  += hist_b[i - 14] * coeff_b[ 7] - hist_a[i - 14] * coeff_a[7 ];
        right += hist_b[i - 13] * coeff_b[ 7] - hist_a[i - 13] * coeff_a[7 ];
        left  += hist_b[i - 16] * coeff_b[ 8] - hist_a[i - 16] * coeff_a[8 ];
        right += hist_b[i - 15] * coeff_b[ 8] - hist_a[i - 15] * coeff_a[8 ];
        left  += hist_b[i - 18] * coeff_b[ 9] - hist_a[i - 18] * coeff_a[9 ];
        right += hist_b[i - 17] * coeff_b[ 9] - hist_a[i - 17] * coeff_a[9 ];
        left  += hist_b[i - 20] * coeff_b[10] - hist_a[i - 20] * coeff_a[10];
        right += hist_b[i - 19] * coeff_b[10] - hist_a[i - 19] * coeff_a[10];
        dst[0] = hist_a[i    ] = (float)left;
        dst[1] = hist_a[i + 1] = (float)right;
        src += 2;
        dst += 2;

        if ((i += 2) == 256) {
            memcpy(hist_a, hist_a + 236, sizeof(*hist_a) * 20);
            memcpy(hist_b, hist_b + 236, sizeof(*hist_b) * 20);
            i = 20;
        }
    }

    s->yule_hist_i = i;
}

/*
 * Calculate the ReplayGain value from the specified loudness histogram;
 * clip to -24 / +64 dB.
 */
static float calc_replaygain(uint32_t *histogram)
{
    uint32_t loud_count = 0, total_windows = 0;
    float gain;
    int i;

    for (i = 0; i < HISTOGRAM_SLOTS; i++)
        total_windows += histogram [i];

    while (i--)
        if ((loud_count += histogram [i]) * 20 >= total_windows)
            break;

    gain = (float)(64.54 - i / 100.0);

    return av_clipf(gain, -24.0, 64.0);
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    ReplayGainContext *s = ctx->priv;
    int64_t level;
    AVFrame *out;

    out = ff_get_audio_buffer(outlink, in->nb_samples);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }

    calc_stereo_peak((float *)in->data[0],
                              in->nb_samples, &s->peak);
    yule_filter_stereo_samples(s, (const float *)in->data[0],
                                        (float *)out->data[0],
                                                 out->nb_samples);
    butter_filter_stereo_samples(s, (float *)out->data[0],
                                             out->nb_samples);
    level = lrint(floor(100 * calc_stereo_rms((float *)out->data[0],
                                                           out->nb_samples)));
    level = av_clip64(level, 0, HISTOGRAM_SLOTS - 1);

    s->histogram[level]++;

    av_frame_free(&out);
    return ff_filter_frame(outlink, in);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    ReplayGainContext *s = ctx->priv;
    float gain = calc_replaygain(s->histogram);

    av_log(ctx, AV_LOG_INFO, "track_gain = %+.2f dB\n", gain);
    av_log(ctx, AV_LOG_INFO, "track_peak = %.6f\n", s->peak);
}

static const AVFilterPad replaygain_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
};

static const AVFilterPad replaygain_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_AUDIO,
    },
};

const AVFilter ff_af_replaygain = {
    .name          = "replaygain",
    .description   = NULL_IF_CONFIG_SMALL("ReplayGain scanner."),
    .uninit        = uninit,
    .priv_size     = sizeof(ReplayGainContext),
    .flags         = AVFILTER_FLAG_METADATA_ONLY,
    FILTER_INPUTS(replaygain_inputs),
    FILTER_OUTPUTS(replaygain_outputs),
    FILTER_QUERY_FUNC(query_formats),
};
