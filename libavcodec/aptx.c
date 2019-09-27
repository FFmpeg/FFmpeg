/*
 * Audio Processing Technology codec for Bluetooth (aptX)
 *
 * Copyright (C) 2017  Aurelien Jacobs <aurel@gnuage.org>
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

#include "libavutil/intreadwrite.h"
#include "avcodec.h"
#include "internal.h"
#include "mathops.h"
#include "audio_frame_queue.h"


enum channels {
    LEFT,
    RIGHT,
    NB_CHANNELS
};

enum subbands {
    LF,  // Low Frequency (0-5.5 kHz)
    MLF, // Medium-Low Frequency (5.5-11kHz)
    MHF, // Medium-High Frequency (11-16.5kHz)
    HF,  // High Frequency (16.5-22kHz)
    NB_SUBBANDS
};

#define NB_FILTERS 2
#define FILTER_TAPS 16

typedef struct {
    int pos;
    int32_t buffer[2*FILTER_TAPS];
} FilterSignal;

typedef struct {
    FilterSignal outer_filter_signal[NB_FILTERS];
    FilterSignal inner_filter_signal[NB_FILTERS][NB_FILTERS];
} QMFAnalysis;

typedef struct {
    int32_t quantized_sample;
    int32_t quantized_sample_parity_change;
    int32_t error;
} Quantize;

typedef struct {
    int32_t quantization_factor;
    int32_t factor_select;
    int32_t reconstructed_difference;
} InvertQuantize;

typedef struct {
    int32_t prev_sign[2];
    int32_t s_weight[2];
    int32_t d_weight[24];
    int32_t pos;
    int32_t reconstructed_differences[48];
    int32_t previous_reconstructed_sample;
    int32_t predicted_difference;
    int32_t predicted_sample;
} Prediction;

typedef struct {
    int32_t codeword_history;
    int32_t dither_parity;
    int32_t dither[NB_SUBBANDS];

    QMFAnalysis qmf;
    Quantize quantize[NB_SUBBANDS];
    InvertQuantize invert_quantize[NB_SUBBANDS];
    Prediction prediction[NB_SUBBANDS];
} Channel;

typedef struct {
    int hd;
    int block_size;
    int32_t sync_idx;
    Channel channels[NB_CHANNELS];
    AudioFrameQueue afq;
} AptXContext;


static const int32_t quantize_intervals_LF[65] = {
      -9948,    9948,   29860,   49808,   69822,   89926,  110144,  130502,
     151026,  171738,  192666,  213832,  235264,  256982,  279014,  301384,
     324118,  347244,  370790,  394782,  419250,  444226,  469742,  495832,
     522536,  549890,  577936,  606720,  636290,  666700,  698006,  730270,
     763562,  797958,  833538,  870398,  908640,  948376,  989740, 1032874,
    1077948, 1125150, 1174700, 1226850, 1281900, 1340196, 1402156, 1468282,
    1539182, 1615610, 1698514, 1789098, 1888944, 2000168, 2125700, 2269750,
    2438670, 2642660, 2899462, 3243240, 3746078, 4535138, 5664098, 7102424,
    8897462,
};
static const int32_t invert_quantize_dither_factors_LF[65] = {
       9948,   9948,   9962,   9988,  10026,  10078,  10142,  10218,
      10306,  10408,  10520,  10646,  10784,  10934,  11098,  11274,
      11462,  11664,  11880,  12112,  12358,  12618,  12898,  13194,
      13510,  13844,  14202,  14582,  14988,  15422,  15884,  16380,
      16912,  17484,  18098,  18762,  19480,  20258,  21106,  22030,
      23044,  24158,  25390,  26760,  28290,  30008,  31954,  34172,
      36728,  39700,  43202,  47382,  52462,  58762,  66770,  77280,
      91642, 112348, 144452, 199326, 303512, 485546, 643414, 794914,
    1000124,
};
static const int32_t quantize_dither_factors_LF[65] = {
        0,     4,     7,    10,    13,    16,    19,    22,
       26,    28,    32,    35,    38,    41,    44,    47,
       51,    54,    58,    62,    65,    70,    74,    79,
       84,    90,    95,   102,   109,   116,   124,   133,
      143,   154,   166,   180,   195,   212,   231,   254,
      279,   308,   343,   383,   430,   487,   555,   639,
      743,   876,  1045,  1270,  1575,  2002,  2628,  3591,
     5177,  8026, 13719, 26047, 45509, 39467, 37875, 51303,
        0,
};
static const int16_t quantize_factor_select_offset_LF[65] = {
      0, -21, -19, -17, -15, -12, -10,  -8,
     -6,  -4,  -1,   1,   3,   6,   8,  10,
     13,  15,  18,  20,  23,  26,  29,  31,
     34,  37,  40,  43,  47,  50,  53,  57,
     60,  64,  68,  72,  76,  80,  85,  89,
     94,  99, 105, 110, 116, 123, 129, 136,
    144, 152, 161, 171, 182, 194, 207, 223,
    241, 263, 291, 328, 382, 467, 522, 522,
    522,
};


static const int32_t quantize_intervals_MLF[9] = {
    -89806, 89806, 278502, 494338, 759442, 1113112, 1652322, 2720256, 5190186,
};
static const int32_t invert_quantize_dither_factors_MLF[9] = {
    89806, 89806, 98890, 116946, 148158, 205512, 333698, 734236, 1735696,
};
static const int32_t quantize_dither_factors_MLF[9] = {
    0, 2271, 4514, 7803, 14339, 32047, 100135, 250365, 0,
};
static const int16_t quantize_factor_select_offset_MLF[9] = {
    0, -14, 6, 29, 58, 96, 154, 270, 521,
};


static const int32_t quantize_intervals_MHF[3] = {
    -194080, 194080, 890562,
};
static const int32_t invert_quantize_dither_factors_MHF[3] = {
    194080, 194080, 502402,
};
static const int32_t quantize_dither_factors_MHF[3] = {
    0, 77081, 0,
};
static const int16_t quantize_factor_select_offset_MHF[3] = {
    0, -33, 136,
};


static const int32_t quantize_intervals_HF[5] = {
    -163006, 163006, 542708, 1120554, 2669238,
};
static const int32_t invert_quantize_dither_factors_HF[5] = {
    163006, 163006, 216698, 361148, 1187538,
};
static const int32_t quantize_dither_factors_HF[5] = {
    0, 13423, 36113, 206598, 0,
};
static const int16_t quantize_factor_select_offset_HF[5] = {
    0, -8, 33, 95, 262,
};


static const int32_t hd_quantize_intervals_LF[257] = {
      -2436,    2436,    7308,   12180,   17054,   21930,   26806,   31686,
      36566,   41450,   46338,   51230,   56124,   61024,   65928,   70836,
      75750,   80670,   85598,   90530,   95470,  100418,  105372,  110336,
     115308,  120288,  125278,  130276,  135286,  140304,  145334,  150374,
     155426,  160490,  165566,  170654,  175756,  180870,  185998,  191138,
     196294,  201466,  206650,  211850,  217068,  222300,  227548,  232814,
     238096,  243396,  248714,  254050,  259406,  264778,  270172,  275584,
     281018,  286470,  291944,  297440,  302956,  308496,  314056,  319640,
     325248,  330878,  336532,  342212,  347916,  353644,  359398,  365178,
     370986,  376820,  382680,  388568,  394486,  400430,  406404,  412408,
     418442,  424506,  430600,  436726,  442884,  449074,  455298,  461554,
     467844,  474168,  480528,  486922,  493354,  499820,  506324,  512866,
     519446,  526064,  532722,  539420,  546160,  552940,  559760,  566624,
     573532,  580482,  587478,  594520,  601606,  608740,  615920,  623148,
     630426,  637754,  645132,  652560,  660042,  667576,  675164,  682808,
     690506,  698262,  706074,  713946,  721876,  729868,  737920,  746036,
     754216,  762460,  770770,  779148,  787594,  796108,  804694,  813354,
     822086,  830892,  839774,  848736,  857776,  866896,  876100,  885386,
     894758,  904218,  913766,  923406,  933138,  942964,  952886,  962908,
     973030,  983254,  993582, 1004020, 1014566, 1025224, 1035996, 1046886,
    1057894, 1069026, 1080284, 1091670, 1103186, 1114838, 1126628, 1138558,
    1150634, 1162858, 1175236, 1187768, 1200462, 1213320, 1226346, 1239548,
    1252928, 1266490, 1280242, 1294188, 1308334, 1322688, 1337252, 1352034,
    1367044, 1382284, 1397766, 1413494, 1429478, 1445728, 1462252, 1479058,
    1496158, 1513562, 1531280, 1549326, 1567710, 1586446, 1605550, 1625034,
    1644914, 1665208, 1685932, 1707108, 1728754, 1750890, 1773542, 1796732,
    1820488, 1844840, 1869816, 1895452, 1921780, 1948842, 1976680, 2005338,
    2034868, 2065322, 2096766, 2129260, 2162880, 2197708, 2233832, 2271352,
    2310384, 2351050, 2393498, 2437886, 2484404, 2533262, 2584710, 2639036,
    2696578, 2757738, 2822998, 2892940, 2968278, 3049896, 3138912, 3236760,
    3345312, 3467068, 3605434, 3765154, 3952904, 4177962, 4452178, 4787134,
    5187290, 5647128, 6159120, 6720518, 7332904, 8000032, 8726664, 9518152,
    10380372,
};
static const int32_t hd_invert_quantize_dither_factors_LF[257] = {
      2436,   2436,   2436,   2436,   2438,   2438,   2438,   2440,
      2442,   2442,   2444,   2446,   2448,   2450,   2454,   2456,
      2458,   2462,   2464,   2468,   2472,   2476,   2480,   2484,
      2488,   2492,   2498,   2502,   2506,   2512,   2518,   2524,
      2528,   2534,   2540,   2548,   2554,   2560,   2568,   2574,
      2582,   2588,   2596,   2604,   2612,   2620,   2628,   2636,
      2646,   2654,   2664,   2672,   2682,   2692,   2702,   2712,
      2722,   2732,   2742,   2752,   2764,   2774,   2786,   2798,
      2810,   2822,   2834,   2846,   2858,   2870,   2884,   2896,
      2910,   2924,   2938,   2952,   2966,   2980,   2994,   3010,
      3024,   3040,   3056,   3070,   3086,   3104,   3120,   3136,
      3154,   3170,   3188,   3206,   3224,   3242,   3262,   3280,
      3300,   3320,   3338,   3360,   3380,   3400,   3422,   3442,
      3464,   3486,   3508,   3532,   3554,   3578,   3602,   3626,
      3652,   3676,   3702,   3728,   3754,   3780,   3808,   3836,
      3864,   3892,   3920,   3950,   3980,   4010,   4042,   4074,
      4106,   4138,   4172,   4206,   4240,   4276,   4312,   4348,
      4384,   4422,   4460,   4500,   4540,   4580,   4622,   4664,
      4708,   4752,   4796,   4842,   4890,   4938,   4986,   5036,
      5086,   5138,   5192,   5246,   5300,   5358,   5416,   5474,
      5534,   5596,   5660,   5726,   5792,   5860,   5930,   6002,
      6074,   6150,   6226,   6306,   6388,   6470,   6556,   6644,
      6736,   6828,   6924,   7022,   7124,   7228,   7336,   7448,
      7562,   7680,   7802,   7928,   8058,   8192,   8332,   8476,
      8624,   8780,   8940,   9106,   9278,   9458,   9644,   9840,
     10042,  10252,  10472,  10702,  10942,  11194,  11458,  11734,
     12024,  12328,  12648,  12986,  13342,  13720,  14118,  14540,
     14990,  15466,  15976,  16520,  17102,  17726,  18398,  19124,
     19908,  20760,  21688,  22702,  23816,  25044,  26404,  27922,
     29622,  31540,  33720,  36222,  39116,  42502,  46514,  51334,
     57218,  64536,  73830,  85890, 101860, 123198, 151020, 183936,
    216220, 243618, 268374, 293022, 319362, 347768, 378864, 412626, 449596,
};
static const int32_t hd_quantize_dither_factors_LF[256] = {
       0,    0,    0,    1,    0,    0,    1,    1,
       0,    1,    1,    1,    1,    1,    1,    1,
       1,    1,    1,    1,    1,    1,    1,    1,
       1,    2,    1,    1,    2,    2,    2,    1,
       2,    2,    2,    2,    2,    2,    2,    2,
       2,    2,    2,    2,    2,    2,    2,    3,
       2,    3,    2,    3,    3,    3,    3,    3,
       3,    3,    3,    3,    3,    3,    3,    3,
       3,    3,    3,    3,    3,    4,    3,    4,
       4,    4,    4,    4,    4,    4,    4,    4,
       4,    4,    4,    4,    5,    4,    4,    5,
       4,    5,    5,    5,    5,    5,    5,    5,
       5,    5,    6,    5,    5,    6,    5,    6,
       6,    6,    6,    6,    6,    6,    6,    7,
       6,    7,    7,    7,    7,    7,    7,    7,
       7,    7,    8,    8,    8,    8,    8,    8,
       8,    9,    9,    9,    9,    9,    9,    9,
      10,   10,   10,   10,   10,   11,   11,   11,
      11,   11,   12,   12,   12,   12,   13,   13,
      13,   14,   14,   14,   15,   15,   15,   15,
      16,   16,   17,   17,   17,   18,   18,   18,
      19,   19,   20,   21,   21,   22,   22,   23,
      23,   24,   25,   26,   26,   27,   28,   29,
      30,   31,   32,   33,   34,   35,   36,   37,
      39,   40,   42,   43,   45,   47,   49,   51,
      53,   55,   58,   60,   63,   66,   69,   73,
      76,   80,   85,   89,   95,  100,  106,  113,
     119,  128,  136,  146,  156,  168,  182,  196,
     213,  232,  254,  279,  307,  340,  380,  425,
     480,  545,  626,  724,  847, 1003, 1205, 1471,
    1830, 2324, 3015, 3993, 5335, 6956, 8229, 8071,
    6850, 6189, 6162, 6585, 7102, 7774, 8441, 9243,
};
static const int16_t hd_quantize_factor_select_offset_LF[257] = {
      0, -22, -21, -21, -20, -20, -19, -19,
    -18, -18, -17, -17, -16, -16, -15, -14,
    -14, -13, -13, -12, -12, -11, -11, -10,
    -10,  -9,  -9,  -8,  -7,  -7,  -6,  -6,
     -5,  -5,  -4,  -4,  -3,  -3,  -2,  -1,
     -1,   0,   0,   1,   1,   2,   2,   3,
      4,   4,   5,   5,   6,   6,   7,   8,
      8,   9,   9,  10,  11,  11,  12,  12,
     13,  14,  14,  15,  15,  16,  17,  17,
     18,  19,  19,  20,  20,  21,  22,  22,
     23,  24,  24,  25,  26,  26,  27,  28,
     28,  29,  30,  30,  31,  32,  33,  33,
     34,  35,  35,  36,  37,  38,  38,  39,
     40,  41,  41,  42,  43,  44,  44,  45,
     46,  47,  48,  48,  49,  50,  51,  52,
     52,  53,  54,  55,  56,  57,  58,  58,
     59,  60,  61,  62,  63,  64,  65,  66,
     67,  68,  69,  69,  70,  71,  72,  73,
     74,  75,  77,  78,  79,  80,  81,  82,
     83,  84,  85,  86,  87,  89,  90,  91,
     92,  93,  94,  96,  97,  98,  99, 101,
    102, 103, 105, 106, 107, 109, 110, 112,
    113, 115, 116, 118, 119, 121, 122, 124,
    125, 127, 129, 130, 132, 134, 136, 137,
    139, 141, 143, 145, 147, 149, 151, 153,
    155, 158, 160, 162, 164, 167, 169, 172,
    174, 177, 180, 182, 185, 188, 191, 194,
    197, 201, 204, 208, 211, 215, 219, 223,
    227, 232, 236, 241, 246, 251, 257, 263,
    269, 275, 283, 290, 298, 307, 317, 327,
    339, 352, 367, 384, 404, 429, 458, 494,
    522, 522, 522, 522, 522, 522, 522, 522, 522,
};


static const int32_t hd_quantize_intervals_MLF[33] = {
      -21236,   21236,   63830,  106798,  150386,  194832,  240376,  287258,
      335726,  386034,  438460,  493308,  550924,  611696,  676082,  744626,
      817986,  896968,  982580, 1076118, 1179278, 1294344, 1424504, 1574386,
     1751090, 1966260, 2240868, 2617662, 3196432, 4176450, 5658260, 7671068,
    10380372,
};
static const int32_t hd_invert_quantize_dither_factors_MLF[33] = {
    21236,  21236,  21360,  21608,  21978,  22468,  23076,   23806,
    24660,  25648,  26778,  28070,  29544,  31228,  33158,   35386,
    37974,  41008,  44606,  48934,  54226,  60840,  69320,   80564,
    96140, 119032, 155576, 221218, 357552, 622468, 859344, 1153464, 1555840,
};
static const int32_t hd_quantize_dither_factors_MLF[32] = {
       0,   31,    62,    93,   123,   152,   183,    214,
     247,  283,   323,   369,   421,   483,   557,    647,
     759,  900,  1082,  1323,  1654,  2120,  2811,   3894,
    5723, 9136, 16411, 34084, 66229, 59219, 73530, 100594,
};
static const int16_t hd_quantize_factor_select_offset_MLF[33] = {
      0, -21, -16, -12,  -7,  -2,   3,   8,
     13,  19,  24,  30,  36,  43,  50,  57,
     65,  74,  83,  93, 104, 117, 131, 147,
    166, 189, 219, 259, 322, 427, 521, 521, 521,
};


static const int32_t hd_quantize_intervals_MHF[9] = {
    -95044, 95044, 295844, 528780, 821332, 1226438, 1890540, 3344850, 6450664,
};
static const int32_t hd_invert_quantize_dither_factors_MHF[9] = {
    95044, 95044, 105754, 127180, 165372, 39736, 424366, 1029946, 2075866,
};
static const int32_t hd_quantize_dither_factors_MHF[8] = {
    0, 2678, 5357, 9548, -31409, 96158, 151395, 261480,
};
static const int16_t hd_quantize_factor_select_offset_MHF[9] = {
    0, -17, 5, 30, 62, 105, 177, 334, 518,
};


static const int32_t hd_quantize_intervals_HF[17] = {
     -45754,   45754,  138496,  234896,  337336,  448310,  570738,  708380,
     866534, 1053262, 1281958, 1577438, 1993050, 2665984, 3900982, 5902844,
    8897462,
};
static const int32_t hd_invert_quantize_dither_factors_HF[17] = {
    45754,  45754,  46988,  49412,  53026,  57950,  64478,   73164,
    84988, 101740, 126958, 168522, 247092, 425842, 809154, 1192708, 1801910,
};
static const int32_t hd_quantize_dither_factors_HF[16] = {
       0,  309,   606,   904,  1231,  1632,  2172,   2956,
    4188, 6305, 10391, 19643, 44688, 95828, 95889, 152301,
};
static const int16_t hd_quantize_factor_select_offset_HF[17] = {
     0, -18,  -8,   2,  13,  25,  38,  53,
    70,  90, 115, 147, 192, 264, 398, 521, 521,
};

typedef const struct {
    const int32_t *quantize_intervals;
    const int32_t *invert_quantize_dither_factors;
    const int32_t *quantize_dither_factors;
    const int16_t *quantize_factor_select_offset;
    int tables_size;
    int32_t factor_max;
    int32_t prediction_order;
} ConstTables;

static ConstTables tables[2][NB_SUBBANDS] = {
    {
        [LF]  = { quantize_intervals_LF,
                  invert_quantize_dither_factors_LF,
                  quantize_dither_factors_LF,
                  quantize_factor_select_offset_LF,
                  FF_ARRAY_ELEMS(quantize_intervals_LF),
                  0x11FF, 24 },
        [MLF] = { quantize_intervals_MLF,
                  invert_quantize_dither_factors_MLF,
                  quantize_dither_factors_MLF,
                  quantize_factor_select_offset_MLF,
                  FF_ARRAY_ELEMS(quantize_intervals_MLF),
                  0x14FF, 12 },
        [MHF] = { quantize_intervals_MHF,
                  invert_quantize_dither_factors_MHF,
                  quantize_dither_factors_MHF,
                  quantize_factor_select_offset_MHF,
                  FF_ARRAY_ELEMS(quantize_intervals_MHF),
                  0x16FF, 6 },
        [HF]  = { quantize_intervals_HF,
                  invert_quantize_dither_factors_HF,
                  quantize_dither_factors_HF,
                  quantize_factor_select_offset_HF,
                  FF_ARRAY_ELEMS(quantize_intervals_HF),
                  0x15FF, 12 },
    },
    {
        [LF]  = { hd_quantize_intervals_LF,
                  hd_invert_quantize_dither_factors_LF,
                  hd_quantize_dither_factors_LF,
                  hd_quantize_factor_select_offset_LF,
                  FF_ARRAY_ELEMS(hd_quantize_intervals_LF),
                  0x11FF, 24 },
        [MLF] = { hd_quantize_intervals_MLF,
                  hd_invert_quantize_dither_factors_MLF,
                  hd_quantize_dither_factors_MLF,
                  hd_quantize_factor_select_offset_MLF,
                  FF_ARRAY_ELEMS(hd_quantize_intervals_MLF),
                  0x14FF, 12 },
        [MHF] = { hd_quantize_intervals_MHF,
                  hd_invert_quantize_dither_factors_MHF,
                  hd_quantize_dither_factors_MHF,
                  hd_quantize_factor_select_offset_MHF,
                  FF_ARRAY_ELEMS(hd_quantize_intervals_MHF),
                  0x16FF, 6 },
        [HF]  = { hd_quantize_intervals_HF,
                  hd_invert_quantize_dither_factors_HF,
                  hd_quantize_dither_factors_HF,
                  hd_quantize_factor_select_offset_HF,
                  FF_ARRAY_ELEMS(hd_quantize_intervals_HF),
                  0x15FF, 12 },
    }
};

static const int16_t quantization_factors[32] = {
    2048, 2093, 2139, 2186, 2233, 2282, 2332, 2383,
    2435, 2489, 2543, 2599, 2656, 2714, 2774, 2834,
    2896, 2960, 3025, 3091, 3158, 3228, 3298, 3371,
    3444, 3520, 3597, 3676, 3756, 3838, 3922, 4008,
};


/* Rounded right shift with optionnal clipping */
#define RSHIFT_SIZE(size)                                                     \
av_always_inline                                                              \
static int##size##_t rshift##size(int##size##_t value, int shift)             \
{                                                                             \
    int##size##_t rounding = (int##size##_t)1 << (shift - 1);                 \
    int##size##_t mask = ((int##size##_t)1 << (shift + 1)) - 1;               \
    return ((value + rounding) >> shift) - ((value & mask) == rounding);      \
}                                                                             \
av_always_inline                                                              \
static int##size##_t rshift##size##_clip24(int##size##_t value, int shift)    \
{                                                                             \
    return av_clip_intp2(rshift##size(value, shift), 23);                     \
}
RSHIFT_SIZE(32)
RSHIFT_SIZE(64)


av_always_inline
static void aptx_update_codeword_history(Channel *channel)
{
    int32_t cw = ((channel->quantize[0].quantized_sample & 3) << 0) +
                 ((channel->quantize[1].quantized_sample & 2) << 1) +
                 ((channel->quantize[2].quantized_sample & 1) << 3);
    channel->codeword_history = (cw << 8) + ((unsigned)channel->codeword_history << 4);
}

static void aptx_generate_dither(Channel *channel)
{
    int subband;
    int64_t m;
    int32_t d;

    aptx_update_codeword_history(channel);

    m = (int64_t)5184443 * (channel->codeword_history >> 7);
    d = (m * 4) + (m >> 22);
    for (subband = 0; subband < NB_SUBBANDS; subband++)
        channel->dither[subband] = (unsigned)d << (23 - 5*subband);
    channel->dither_parity = (d >> 25) & 1;
}

/*
 * Convolution filter coefficients for the outer QMF of the QMF tree.
 * The 2 sets are a mirror of each other.
 */
static const int32_t aptx_qmf_outer_coeffs[NB_FILTERS][FILTER_TAPS] = {
    {
        730, -413, -9611, 43626, -121026, 269973, -585547, 2801966,
        697128, -160481, 27611, 8478, -10043, 3511, 688, -897,
    },
    {
        -897, 688, 3511, -10043, 8478, 27611, -160481, 697128,
        2801966, -585547, 269973, -121026, 43626, -9611, -413, 730,
    },
};

/*
 * Convolution filter coefficients for the inner QMF of the QMF tree.
 * The 2 sets are a mirror of each other.
 */
static const int32_t aptx_qmf_inner_coeffs[NB_FILTERS][FILTER_TAPS] = {
    {
       1033, -584, -13592, 61697, -171156, 381799, -828088, 3962579,
       985888, -226954, 39048, 11990, -14203, 4966, 973, -1268,
    },
    {
      -1268, 973, 4966, -14203, 11990, 39048, -226954, 985888,
      3962579, -828088, 381799, -171156, 61697, -13592, -584, 1033,
    },
};

/*
 * Push one sample into a circular signal buffer.
 */
av_always_inline
static void aptx_qmf_filter_signal_push(FilterSignal *signal, int32_t sample)
{
    signal->buffer[signal->pos            ] = sample;
    signal->buffer[signal->pos+FILTER_TAPS] = sample;
    signal->pos = (signal->pos + 1) & (FILTER_TAPS - 1);
}

/*
 * Compute the convolution of the signal with the coefficients, and reduce
 * to 24 bits by applying the specified right shifting.
 */
av_always_inline
static int32_t aptx_qmf_convolution(FilterSignal *signal,
                                    const int32_t coeffs[FILTER_TAPS],
                                    int shift)
{
    int32_t *sig = &signal->buffer[signal->pos];
    int64_t e = 0;
    int i;

    for (i = 0; i < FILTER_TAPS; i++)
        e += MUL64(sig[i], coeffs[i]);

    return rshift64_clip24(e, shift);
}

/*
 * Half-band QMF analysis filter realized with a polyphase FIR filter.
 * Split into 2 subbands and downsample by 2.
 * So for each pair of samples that goes in, one sample goes out,
 * split into 2 separate subbands.
 */
av_always_inline
static void aptx_qmf_polyphase_analysis(FilterSignal signal[NB_FILTERS],
                                        const int32_t coeffs[NB_FILTERS][FILTER_TAPS],
                                        int shift,
                                        int32_t samples[NB_FILTERS],
                                        int32_t *low_subband_output,
                                        int32_t *high_subband_output)
{
    int32_t subbands[NB_FILTERS];
    int i;

    for (i = 0; i < NB_FILTERS; i++) {
        aptx_qmf_filter_signal_push(&signal[i], samples[NB_FILTERS-1-i]);
        subbands[i] = aptx_qmf_convolution(&signal[i], coeffs[i], shift);
    }

    *low_subband_output  = av_clip_intp2(subbands[0] + subbands[1], 23);
    *high_subband_output = av_clip_intp2(subbands[0] - subbands[1], 23);
}

/*
 * Two stage QMF analysis tree.
 * Split 4 input samples into 4 subbands and downsample by 4.
 * So for each group of 4 samples that goes in, one sample goes out,
 * split into 4 separate subbands.
 */
static void aptx_qmf_tree_analysis(QMFAnalysis *qmf,
                                   int32_t samples[4],
                                   int32_t subband_samples[4])
{
    int32_t intermediate_samples[4];
    int i;

    /* Split 4 input samples into 2 intermediate subbands downsampled to 2 samples */
    for (i = 0; i < 2; i++)
        aptx_qmf_polyphase_analysis(qmf->outer_filter_signal,
                                    aptx_qmf_outer_coeffs, 23,
                                    &samples[2*i],
                                    &intermediate_samples[0+i],
                                    &intermediate_samples[2+i]);

    /* Split 2 intermediate subband samples into 4 final subbands downsampled to 1 sample */
    for (i = 0; i < 2; i++)
        aptx_qmf_polyphase_analysis(qmf->inner_filter_signal[i],
                                    aptx_qmf_inner_coeffs, 23,
                                    &intermediate_samples[2*i],
                                    &subband_samples[2*i+0],
                                    &subband_samples[2*i+1]);
}

/*
 * Half-band QMF synthesis filter realized with a polyphase FIR filter.
 * Join 2 subbands and upsample by 2.
 * So for each 2 subbands sample that goes in, a pair of samples goes out.
 */
av_always_inline
static void aptx_qmf_polyphase_synthesis(FilterSignal signal[NB_FILTERS],
                                         const int32_t coeffs[NB_FILTERS][FILTER_TAPS],
                                         int shift,
                                         int32_t low_subband_input,
                                         int32_t high_subband_input,
                                         int32_t samples[NB_FILTERS])
{
    int32_t subbands[NB_FILTERS];
    int i;

    subbands[0] = low_subband_input + high_subband_input;
    subbands[1] = low_subband_input - high_subband_input;

    for (i = 0; i < NB_FILTERS; i++) {
        aptx_qmf_filter_signal_push(&signal[i], subbands[1-i]);
        samples[i] = aptx_qmf_convolution(&signal[i], coeffs[i], shift);
    }
}

/*
 * Two stage QMF synthesis tree.
 * Join 4 subbands and upsample by 4.
 * So for each 4 subbands sample that goes in, a group of 4 samples goes out.
 */
static void aptx_qmf_tree_synthesis(QMFAnalysis *qmf,
                                    int32_t subband_samples[4],
                                    int32_t samples[4])
{
    int32_t intermediate_samples[4];
    int i;

    /* Join 4 subbands into 2 intermediate subbands upsampled to 2 samples. */
    for (i = 0; i < 2; i++)
        aptx_qmf_polyphase_synthesis(qmf->inner_filter_signal[i],
                                     aptx_qmf_inner_coeffs, 22,
                                     subband_samples[2*i+0],
                                     subband_samples[2*i+1],
                                     &intermediate_samples[2*i]);

    /* Join 2 samples from intermediate subbands upsampled to 4 samples. */
    for (i = 0; i < 2; i++)
        aptx_qmf_polyphase_synthesis(qmf->outer_filter_signal,
                                     aptx_qmf_outer_coeffs, 21,
                                     intermediate_samples[0+i],
                                     intermediate_samples[2+i],
                                     &samples[2*i]);
}


av_always_inline
static int32_t aptx_bin_search(int32_t value, int32_t factor,
                               const int32_t *intervals, int32_t nb_intervals)
{
    int32_t idx = 0;
    int i;

    for (i = nb_intervals >> 1; i > 0; i >>= 1)
        if (MUL64(factor, intervals[idx + i]) <= ((int64_t)value << 24))
            idx += i;

    return idx;
}

static void aptx_quantize_difference(Quantize *quantize,
                                     int32_t sample_difference,
                                     int32_t dither,
                                     int32_t quantization_factor,
                                     ConstTables *tables)
{
    const int32_t *intervals = tables->quantize_intervals;
    int32_t quantized_sample, dithered_sample, parity_change;
    int32_t d, mean, interval, inv, sample_difference_abs;
    int64_t error;

    sample_difference_abs = FFABS(sample_difference);
    sample_difference_abs = FFMIN(sample_difference_abs, (1 << 23) - 1);

    quantized_sample = aptx_bin_search(sample_difference_abs >> 4,
                                       quantization_factor,
                                       intervals, tables->tables_size);

    d = rshift32_clip24(MULH(dither, dither), 7) - (1 << 23);
    d = rshift64(MUL64(d, tables->quantize_dither_factors[quantized_sample]), 23);

    intervals += quantized_sample;
    mean = (intervals[1] + intervals[0]) / 2;
    interval = (intervals[1] - intervals[0]) * (-(sample_difference < 0) | 1);

    dithered_sample = rshift64_clip24(MUL64(dither, interval) + ((int64_t)av_clip_intp2(mean + d, 23) << 32), 32);
    error = ((int64_t)sample_difference_abs << 20) - MUL64(dithered_sample, quantization_factor);
    quantize->error = FFABS(rshift64(error, 23));

    parity_change = quantized_sample;
    if (error < 0)
        quantized_sample--;
    else
        parity_change--;

    inv = -(sample_difference < 0);
    quantize->quantized_sample               = quantized_sample ^ inv;
    quantize->quantized_sample_parity_change = parity_change    ^ inv;
}

static void aptx_encode_channel(Channel *channel, int32_t samples[4], int hd)
{
    int32_t subband_samples[4];
    int subband;
    aptx_qmf_tree_analysis(&channel->qmf, samples, subband_samples);
    aptx_generate_dither(channel);
    for (subband = 0; subband < NB_SUBBANDS; subband++) {
        int32_t diff = av_clip_intp2(subband_samples[subband] - channel->prediction[subband].predicted_sample, 23);
        aptx_quantize_difference(&channel->quantize[subband], diff,
                                 channel->dither[subband],
                                 channel->invert_quantize[subband].quantization_factor,
                                 &tables[hd][subband]);
    }
}

static void aptx_decode_channel(Channel *channel, int32_t samples[4])
{
    int32_t subband_samples[4];
    int subband;
    for (subband = 0; subband < NB_SUBBANDS; subband++)
        subband_samples[subband] = channel->prediction[subband].previous_reconstructed_sample;
    aptx_qmf_tree_synthesis(&channel->qmf, subband_samples, samples);
}


static void aptx_invert_quantization(InvertQuantize *invert_quantize,
                                     int32_t quantized_sample, int32_t dither,
                                     ConstTables *tables)
{
    int32_t qr, idx, shift, factor_select;

    idx = (quantized_sample ^ -(quantized_sample < 0)) + 1;
    qr = tables->quantize_intervals[idx] / 2;
    if (quantized_sample < 0)
        qr = -qr;

    qr = rshift64_clip24((qr * (1LL<<32)) + MUL64(dither, tables->invert_quantize_dither_factors[idx]), 32);
    invert_quantize->reconstructed_difference = MUL64(invert_quantize->quantization_factor, qr) >> 19;

    /* update factor_select */
    factor_select = 32620 * invert_quantize->factor_select;
    factor_select = rshift32(factor_select + (tables->quantize_factor_select_offset[idx] * (1 << 15)), 15);
    invert_quantize->factor_select = av_clip(factor_select, 0, tables->factor_max);

    /* update quantization factor */
    idx = (invert_quantize->factor_select & 0xFF) >> 3;
    shift = (tables->factor_max - invert_quantize->factor_select) >> 8;
    invert_quantize->quantization_factor = (quantization_factors[idx] << 11) >> shift;
}

static int32_t *aptx_reconstructed_differences_update(Prediction *prediction,
                                                      int32_t reconstructed_difference,
                                                      int order)
{
    int32_t *rd1 = prediction->reconstructed_differences, *rd2 = rd1 + order;
    int p = prediction->pos;

    rd1[p] = rd2[p];
    prediction->pos = p = (p + 1) % order;
    rd2[p] = reconstructed_difference;
    return &rd2[p];
}

static void aptx_prediction_filtering(Prediction *prediction,
                                      int32_t reconstructed_difference,
                                      int order)
{
    int32_t reconstructed_sample, predictor, srd0;
    int32_t *reconstructed_differences;
    int64_t predicted_difference = 0;
    int i;

    reconstructed_sample = av_clip_intp2(reconstructed_difference + prediction->predicted_sample, 23);
    predictor = av_clip_intp2((MUL64(prediction->s_weight[0], prediction->previous_reconstructed_sample)
                             + MUL64(prediction->s_weight[1], reconstructed_sample)) >> 22, 23);
    prediction->previous_reconstructed_sample = reconstructed_sample;

    reconstructed_differences = aptx_reconstructed_differences_update(prediction, reconstructed_difference, order);
    srd0 = FFDIFFSIGN(reconstructed_difference, 0) * (1 << 23);
    for (i = 0; i < order; i++) {
        int32_t srd = FF_SIGNBIT(reconstructed_differences[-i-1]) | 1;
        prediction->d_weight[i] -= rshift32(prediction->d_weight[i] - srd*srd0, 8);
        predicted_difference += MUL64(reconstructed_differences[-i], prediction->d_weight[i]);
    }

    prediction->predicted_difference = av_clip_intp2(predicted_difference >> 22, 23);
    prediction->predicted_sample = av_clip_intp2(predictor + prediction->predicted_difference, 23);
}

static void aptx_process_subband(InvertQuantize *invert_quantize,
                                 Prediction *prediction,
                                 int32_t quantized_sample, int32_t dither,
                                 ConstTables *tables)
{
    int32_t sign, same_sign[2], weight[2], sw1, range;

    aptx_invert_quantization(invert_quantize, quantized_sample, dither, tables);

    sign = FFDIFFSIGN(invert_quantize->reconstructed_difference,
                      -prediction->predicted_difference);
    same_sign[0] = sign * prediction->prev_sign[0];
    same_sign[1] = sign * prediction->prev_sign[1];
    prediction->prev_sign[0] = prediction->prev_sign[1];
    prediction->prev_sign[1] = sign | 1;

    range = 0x100000;
    sw1 = rshift32(-same_sign[1] * prediction->s_weight[1], 1);
    sw1 = (av_clip(sw1, -range, range) & ~0xF) * 16;

    range = 0x300000;
    weight[0] = 254 * prediction->s_weight[0] + 0x800000*same_sign[0] + sw1;
    prediction->s_weight[0] = av_clip(rshift32(weight[0], 8), -range, range);

    range = 0x3C0000 - prediction->s_weight[0];
    weight[1] = 255 * prediction->s_weight[1] + 0xC00000*same_sign[1];
    prediction->s_weight[1] = av_clip(rshift32(weight[1], 8), -range, range);

    aptx_prediction_filtering(prediction,
                              invert_quantize->reconstructed_difference,
                              tables->prediction_order);
}

static void aptx_invert_quantize_and_prediction(Channel *channel, int hd)
{
    int subband;
    for (subband = 0; subband < NB_SUBBANDS; subband++)
        aptx_process_subband(&channel->invert_quantize[subband],
                             &channel->prediction[subband],
                             channel->quantize[subband].quantized_sample,
                             channel->dither[subband],
                             &tables[hd][subband]);
}

static int32_t aptx_quantized_parity(Channel *channel)
{
    int32_t parity = channel->dither_parity;
    int subband;

    for (subband = 0; subband < NB_SUBBANDS; subband++)
        parity ^= channel->quantize[subband].quantized_sample;

    return parity & 1;
}

/* For each sample, ensure that the parity of all subbands of all channels
 * is 0 except once every 8 samples where the parity is forced to 1. */
static int aptx_check_parity(Channel channels[NB_CHANNELS], int32_t *idx)
{
    int32_t parity = aptx_quantized_parity(&channels[LEFT])
                   ^ aptx_quantized_parity(&channels[RIGHT]);

    int eighth = *idx == 7;
    *idx = (*idx + 1) & 7;

    return parity ^ eighth;
}

static void aptx_insert_sync(Channel channels[NB_CHANNELS], int32_t *idx)
{
    if (aptx_check_parity(channels, idx)) {
        int i;
        Channel *c;
        static const int map[] = { 1, 2, 0, 3 };
        Quantize *min = &channels[NB_CHANNELS-1].quantize[map[0]];
        for (c = &channels[NB_CHANNELS-1]; c >= channels; c--)
            for (i = 0; i < NB_SUBBANDS; i++)
                if (c->quantize[map[i]].error < min->error)
                    min = &c->quantize[map[i]];

        /* Forcing the desired parity is done by offsetting by 1 the quantized
         * sample from the subband featuring the smallest quantization error. */
        min->quantized_sample = min->quantized_sample_parity_change;
    }
}

static uint16_t aptx_pack_codeword(Channel *channel)
{
    int32_t parity = aptx_quantized_parity(channel);
    return (((channel->quantize[3].quantized_sample & 0x06) | parity) << 13)
         | (((channel->quantize[2].quantized_sample & 0x03)         ) << 11)
         | (((channel->quantize[1].quantized_sample & 0x0F)         ) <<  7)
         | (((channel->quantize[0].quantized_sample & 0x7F)         ) <<  0);
}

static uint32_t aptxhd_pack_codeword(Channel *channel)
{
    int32_t parity = aptx_quantized_parity(channel);
    return (((channel->quantize[3].quantized_sample & 0x01E) | parity) << 19)
         | (((channel->quantize[2].quantized_sample & 0x00F)         ) << 15)
         | (((channel->quantize[1].quantized_sample & 0x03F)         ) <<  9)
         | (((channel->quantize[0].quantized_sample & 0x1FF)         ) <<  0);
}

static void aptx_unpack_codeword(Channel *channel, uint16_t codeword)
{
    channel->quantize[0].quantized_sample = sign_extend(codeword >>  0, 7);
    channel->quantize[1].quantized_sample = sign_extend(codeword >>  7, 4);
    channel->quantize[2].quantized_sample = sign_extend(codeword >> 11, 2);
    channel->quantize[3].quantized_sample = sign_extend(codeword >> 13, 3);
    channel->quantize[3].quantized_sample = (channel->quantize[3].quantized_sample & ~1)
                                          | aptx_quantized_parity(channel);
}

static void aptxhd_unpack_codeword(Channel *channel, uint32_t codeword)
{
    channel->quantize[0].quantized_sample = sign_extend(codeword >>  0, 9);
    channel->quantize[1].quantized_sample = sign_extend(codeword >>  9, 6);
    channel->quantize[2].quantized_sample = sign_extend(codeword >> 15, 4);
    channel->quantize[3].quantized_sample = sign_extend(codeword >> 19, 5);
    channel->quantize[3].quantized_sample = (channel->quantize[3].quantized_sample & ~1)
                                          | aptx_quantized_parity(channel);
}

static void aptx_encode_samples(AptXContext *ctx,
                                int32_t samples[NB_CHANNELS][4],
                                uint8_t *output)
{
    int channel;
    for (channel = 0; channel < NB_CHANNELS; channel++)
        aptx_encode_channel(&ctx->channels[channel], samples[channel], ctx->hd);

    aptx_insert_sync(ctx->channels, &ctx->sync_idx);

    for (channel = 0; channel < NB_CHANNELS; channel++) {
        aptx_invert_quantize_and_prediction(&ctx->channels[channel], ctx->hd);
        if (ctx->hd)
            AV_WB24(output + 3*channel,
                    aptxhd_pack_codeword(&ctx->channels[channel]));
        else
            AV_WB16(output + 2*channel,
                    aptx_pack_codeword(&ctx->channels[channel]));
    }
}

static int aptx_decode_samples(AptXContext *ctx,
                                const uint8_t *input,
                                int32_t samples[NB_CHANNELS][4])
{
    int channel, ret;

    for (channel = 0; channel < NB_CHANNELS; channel++) {
        aptx_generate_dither(&ctx->channels[channel]);

        if (ctx->hd)
            aptxhd_unpack_codeword(&ctx->channels[channel],
                                   AV_RB24(input + 3*channel));
        else
            aptx_unpack_codeword(&ctx->channels[channel],
                                 AV_RB16(input + 2*channel));
        aptx_invert_quantize_and_prediction(&ctx->channels[channel], ctx->hd);
    }

    ret = aptx_check_parity(ctx->channels, &ctx->sync_idx);

    for (channel = 0; channel < NB_CHANNELS; channel++)
        aptx_decode_channel(&ctx->channels[channel], samples[channel]);

    return ret;
}


static av_cold int aptx_init(AVCodecContext *avctx)
{
    AptXContext *s = avctx->priv_data;
    int chan, subband;

    s->hd = avctx->codec->id == AV_CODEC_ID_APTX_HD;
    s->block_size = s->hd ? 6 : 4;

    if (avctx->frame_size == 0)
        avctx->frame_size = 256 * s->block_size;

    if (avctx->frame_size % s->block_size) {
        av_log(avctx, AV_LOG_ERROR,
               "Frame size must be a multiple of %d samples\n", s->block_size);
        return AVERROR(EINVAL);
    }

    for (chan = 0; chan < NB_CHANNELS; chan++) {
        Channel *channel = &s->channels[chan];
        for (subband = 0; subband < NB_SUBBANDS; subband++) {
            Prediction *prediction = &channel->prediction[subband];
            prediction->prev_sign[0] = 1;
            prediction->prev_sign[1] = 1;
        }
    }

    ff_af_queue_init(avctx, &s->afq);
    return 0;
}

static int aptx_decode_frame(AVCodecContext *avctx, void *data,
                             int *got_frame_ptr, AVPacket *avpkt)
{
    AptXContext *s = avctx->priv_data;
    AVFrame *frame = data;
    int pos, opos, channel, sample, ret;

    if (avpkt->size < s->block_size) {
        av_log(avctx, AV_LOG_ERROR, "Packet is too small\n");
        return AVERROR_INVALIDDATA;
    }

    /* get output buffer */
    frame->channels = NB_CHANNELS;
    frame->format = AV_SAMPLE_FMT_S32P;
    frame->nb_samples = 4 * avpkt->size / s->block_size;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    for (pos = 0, opos = 0; opos < frame->nb_samples; pos += s->block_size, opos += 4) {
        int32_t samples[NB_CHANNELS][4];

        if (aptx_decode_samples(s, &avpkt->data[pos], samples)) {
            av_log(avctx, AV_LOG_ERROR, "Synchronization error\n");
            return AVERROR_INVALIDDATA;
        }

        for (channel = 0; channel < NB_CHANNELS; channel++)
            for (sample = 0; sample < 4; sample++)
                AV_WN32A(&frame->data[channel][4*(opos+sample)],
                         samples[channel][sample] * 256);
    }

    *got_frame_ptr = 1;
    return s->block_size * frame->nb_samples / 4;
}

static int aptx_encode_frame(AVCodecContext *avctx, AVPacket *avpkt,
                             const AVFrame *frame, int *got_packet_ptr)
{
    AptXContext *s = avctx->priv_data;
    int pos, ipos, channel, sample, output_size, ret;

    if ((ret = ff_af_queue_add(&s->afq, frame)) < 0)
        return ret;

    output_size = s->block_size * frame->nb_samples/4;
    if ((ret = ff_alloc_packet2(avctx, avpkt, output_size, 0)) < 0)
        return ret;

    for (pos = 0, ipos = 0; pos < output_size; pos += s->block_size, ipos += 4) {
        int32_t samples[NB_CHANNELS][4];

        for (channel = 0; channel < NB_CHANNELS; channel++)
            for (sample = 0; sample < 4; sample++)
                samples[channel][sample] = (int32_t)AV_RN32A(&frame->data[channel][4*(ipos+sample)]) >> 8;

        aptx_encode_samples(s, samples, avpkt->data + pos);
    }

    ff_af_queue_remove(&s->afq, frame->nb_samples, &avpkt->pts, &avpkt->duration);
    *got_packet_ptr = 1;
    return 0;
}

static av_cold int aptx_close(AVCodecContext *avctx)
{
    AptXContext *s = avctx->priv_data;
    ff_af_queue_close(&s->afq);
    return 0;
}


#if CONFIG_APTX_DECODER
AVCodec ff_aptx_decoder = {
    .name                  = "aptx",
    .long_name             = NULL_IF_CONFIG_SMALL("aptX (Audio Processing Technology for Bluetooth)"),
    .type                  = AVMEDIA_TYPE_AUDIO,
    .id                    = AV_CODEC_ID_APTX,
    .priv_data_size        = sizeof(AptXContext),
    .init                  = aptx_init,
    .decode                = aptx_decode_frame,
    .close                 = aptx_close,
    .capabilities          = AV_CODEC_CAP_DR1,
    .caps_internal         = FF_CODEC_CAP_INIT_THREADSAFE,
    .channel_layouts       = (const uint64_t[]) { AV_CH_LAYOUT_STEREO, 0},
    .sample_fmts           = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_S32P,
                                                             AV_SAMPLE_FMT_NONE },
};
#endif

#if CONFIG_APTX_HD_DECODER
AVCodec ff_aptx_hd_decoder = {
    .name                  = "aptx_hd",
    .long_name             = NULL_IF_CONFIG_SMALL("aptX HD (Audio Processing Technology for Bluetooth)"),
    .type                  = AVMEDIA_TYPE_AUDIO,
    .id                    = AV_CODEC_ID_APTX_HD,
    .priv_data_size        = sizeof(AptXContext),
    .init                  = aptx_init,
    .decode                = aptx_decode_frame,
    .close                 = aptx_close,
    .capabilities          = AV_CODEC_CAP_DR1,
    .caps_internal         = FF_CODEC_CAP_INIT_THREADSAFE,
    .channel_layouts       = (const uint64_t[]) { AV_CH_LAYOUT_STEREO, 0},
    .sample_fmts           = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_S32P,
                                                             AV_SAMPLE_FMT_NONE },
};
#endif

#if CONFIG_APTX_ENCODER
AVCodec ff_aptx_encoder = {
    .name                  = "aptx",
    .long_name             = NULL_IF_CONFIG_SMALL("aptX (Audio Processing Technology for Bluetooth)"),
    .type                  = AVMEDIA_TYPE_AUDIO,
    .id                    = AV_CODEC_ID_APTX,
    .priv_data_size        = sizeof(AptXContext),
    .init                  = aptx_init,
    .encode2               = aptx_encode_frame,
    .close                 = aptx_close,
    .capabilities          = AV_CODEC_CAP_SMALL_LAST_FRAME,
    .caps_internal         = FF_CODEC_CAP_INIT_THREADSAFE,
    .channel_layouts       = (const uint64_t[]) { AV_CH_LAYOUT_STEREO, 0},
    .sample_fmts           = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_S32P,
                                                             AV_SAMPLE_FMT_NONE },
    .supported_samplerates = (const int[]) {8000, 16000, 24000, 32000, 44100, 48000, 0},
};
#endif

#if CONFIG_APTX_HD_ENCODER
AVCodec ff_aptx_hd_encoder = {
    .name                  = "aptx_hd",
    .long_name             = NULL_IF_CONFIG_SMALL("aptX HD (Audio Processing Technology for Bluetooth)"),
    .type                  = AVMEDIA_TYPE_AUDIO,
    .id                    = AV_CODEC_ID_APTX_HD,
    .priv_data_size        = sizeof(AptXContext),
    .init                  = aptx_init,
    .encode2               = aptx_encode_frame,
    .close                 = aptx_close,
    .capabilities          = AV_CODEC_CAP_SMALL_LAST_FRAME,
    .caps_internal         = FF_CODEC_CAP_INIT_THREADSAFE,
    .channel_layouts       = (const uint64_t[]) { AV_CH_LAYOUT_STEREO, 0},
    .sample_fmts           = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_S32P,
                                                             AV_SAMPLE_FMT_NONE },
    .supported_samplerates = (const int[]) {8000, 16000, 24000, 32000, 44100, 48000, 0},
};
#endif
