/*
 * Siren audio decoder
 * Copyright (c) 2012 Youness Alaoui <kakaroto@kakaroto.homelinux.net>
 * Copyright (c) 2018 Paul B Mahol
 * Copyright (c) 2019 Lynne <dev@lynne.ee>
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

#include "libavutil/channel_layout.h"
#include "libavutil/tx.h"
#include "libavutil/float_dsp.h"
#include "libavutil/mem_internal.h"

#include "avcodec.h"
#include "codec_internal.h"
#include "decode.h"
#include "get_bits.h"

static const uint8_t index_table[8] = {4, 4, 3, 3, 2, 2, 1, 0};
static const uint8_t vector_dimension[8] = { 2, 2, 2, 4, 4, 5, 5, 1 };
static const uint8_t number_of_vectors[8] = { 10, 10, 10, 5, 5, 4, 4, 20 };
static const uint8_t expected_bits_table[8] = { 52, 47, 43, 37, 29, 22, 16, 0 };
static const int8_t differential_decoder_tree[27][24][2] = {
    {
        {1, 2}, {3, 4}, {5, 6}, {7, 8}, {9, 10}, {11, -12}, {-11, -10}, {-8, -9}, {-7, -6}, {-13, 12},
        {-5, -4}, {0, 13}, {-3, -14}, {-2, 14}, {-1, 15}, {-15, 16}, {-16, 17}, {-17, 18}, {19, 20},
        {21, 22}, {-18, -19}, {-20, -21}, {-22, -23}, {-32, -32}
    },
    {
        {1, 2}, {3, 4}, {5, 6}, {7, 8}, {-10, -9}, {-8, -11}, {-7, -6}, {9, -5}, {10, -12}, {-4, 11},
        {-13, -3}, {12, -2}, {13, -14}, {-1, 14}, {15, -15}, {0, 16}, {-16, 17}, {-17, 18}, {-18, 19},
        {20, 21},{22, -19}, {-20, -21}, {-22, -23}, {-32, -32}
    },
    {
        {1, 2}, {3, 4}, {5, 6}, {7, 8}, {9, 10}, {-12, 11}, {-11, -13}, {-10, -9}, {12, -14}, {-8, -7},
        {-15, -6}, {13, -5}, {-16, -4}, {14, -17}, {15, -3}, {16, -18}, {-2, 17}, {18, -19}, {-1, 19},
        {-20, 20}, {0, 21}, {22, -21}, {-22, -23}, {-32, -32}
    },
    {
        {1, 2}, {3, 4}, {5, 6}, {-11, -10}, {7, -12}, {8, -9}, {9, -13}, {-14, 10}, {-8, -15}, {-16, 11},
        {-7, 12}, {-17, -6}, {13, 14}, {-18, 15}, {-5, -4}, {16, 17}, {-3, -2}, {-19, 18}, {-1, 19},
        {-20, 20}, {21, 22}, {0, -21}, {-22, -23}, {-32, -32}
    },
    {
        {1, 2}, {3, 4}, {5, 6}, {-12, -11}, {-13, 7}, {8, -14}, {-10, 9}, {10, -15}, {-9, 11}, {-8, 12},
        {-16, 13}, {-7, -6}, {-17, 14}, {-5, -18}, {15, -4}, {16, -19}, {17, -3}, {-20, 18}, {-2, 19},
        {-21, 20}, {0, 21}, {22, -1}, {-22, -23}, {-32, -32}
    },
    {
        {1, 2}, {3, 4}, {5, 6}, {-11, 7}, {-12, -10}, {-13, -9}, {8, 9}, {-14, -8}, {10, -15}, {-7, 11},
        {-16, 12}, {-6, -17}, {13, 14}, {-5, 15}, {-18, 16}, {-4, 17}, {-3, -19}, {18, -2}, {-20, 19},
        {-1, 20}, {0, 21}, {22, -21}, {-22, -23}, {-32, -32}
    },
    {
        {1, 2}, {3, 4}, {5, -12}, {6, -11}, {-10, -13}, {-9, 7}, {8, -14}, {9, -8}, {-15, 10}, {-7, -16},
        {11, -6}, {12, -17}, {13, -5}, {-18, 14}, {15, -4}, {-19, 16}, {17, -3}, {-20, 18}, {19, 20},
        {21, 22}, {0, -2}, {-1, -21}, {-22, -23}, {-32, -32}
    },
    {
        {1, 2}, {3, 4}, {5, -12}, {6, -13}, {-11, -10}, {7, -14}, {8, -9}, {9, -15}, {-8, 10}, {-7, -16},
        {11, 12}, {-6, -17}, {-5, 13}, {14, 15}, {-18, -4}, {-19, 16}, {-3, 17}, {18, -2}, {-20, 19},
        {20, 21}, {22, 0}, {-1, -21}, {-22, -23}, {-32, -32}
    },
    {
        {1, 2}, {3, 4}, {5, 6}, {-11, -10}, {-12, -9}, {7, 8}, {-13, -8}, {9, -14}, {-7, 10}, {-6, -15},
        {11, 12}, {-5, -16}, {13, 14}, {-17, 15}, {-4, 16}, {17, -18}, {18, -3}, {-2, 19}, {-1, 0},
        {-19, 20}, {-20, 21}, {22, -21}, {-22, -23}, {-32, -32}
    },
    {
        {1, 2}, {3, 4}, {5, 6}, {-11, 7}, {-10, -12}, {-9, 8}, {-8, -13}, {9, -7}, {10, -14}, {-6, 11},
        {-15, 12}, {-5, 13}, {-16, -4}, {14, 15}, {-17, -3}, {-18, 16}, {17, -19}, {-2, 18}, {-20, 19},
        {-1, 20}, {21, 22}, {0, -21}, {-22, -23}, {-32, -32}
    },
    {
        {1, 2}, {3, 4}, {5, -12}, {6, -11}, {7, 8}, {-10, -13}, {-9, 9}, {-8, -14}, {10, -7}, {11, -15},
        {-6, 12}, {-5, 13}, {-4, -16}, {14, 15}, {-3, -17}, {16, 17}, {-18, -2}, {18, -19}, {-1, 19},
        {-20, 20}, {-21, 21}, {22, 0}, {-22, -23}, {-32, -32}
    },
    {
        {1, 2}, {3, 4}, {5, -12}, {-13, 6}, {-11, 7}, {-14, 8}, {-10, 9}, {-15, -9}, {-8, 10}, {-7, -16},
        {11, -6}, {12, -5}, {-17, 13}, {14, -18}, {15, -4}, {16, -19}, {17, -3}, {18, -2}, {19, -1},
        {-20, 20}, {21, 22}, {0, -21}, {-22, -23}, {-32, -32}
    },
    {
        {1, 2}, {3, 4}, {-12, 5}, {-11, -13}, {6, -14}, {-10, 7}, {8, -15}, {-9, 9}, {-16, 10}, {-8, -17},
        {11, 12}, {-7, -18}, {-6, 13}, {14, -5}, {15, -19}, {-4, 16}, {-20, 17}, {18, 19}, {20, 21},
        {22, 0}, {-1, -3}, {-2, -21}, {-22, -23}, {-32, -32}
    },
    {
        {1, 2}, {3, 4}, {-12, 5}, {-11, -13}, {6, -14}, {-10, 7}, {8, -15}, {-9, 9}, {-16, 10}, {-8, -17},
        {11, 12}, {-7, -18}, {-6, 13}, {14, -5}, {15, -19}, {-4, 16}, {-20, 17}, {18, 19}, {20, 21},
        {22, 0}, {-1, -3}, {-2, -21}, {-22, -23}, {-32, -32}
    },
    {
        {1, 2}, {3, 4}, {-12, 5}, {-11, -13}, {6, -14}, {-10, 7}, {8, -15}, {-9, 9}, {-16, 10}, {-8, -17},
        {11, 12}, {-7, -18}, {-6, 13}, {14, -5}, {15, -19}, {-4, 16}, {-20, 17}, {18, 19}, {20, 21},
        {22, 0}, {-1, -3}, {-2, -21}, {-22, -23}, {-32, -32}
    },
    {
        {1, 2}, {3, 4}, {-12, 5}, {-11, -13}, {6, -14}, {-10, 7}, {8, -15}, {-9, 9}, {-16, 10}, {-8, -17},
        {11, 12}, {-7, -18}, {-6, 13}, {14, -5}, {15, -19}, {-4, 16}, {-20, 17}, {18, 19}, {20, 21},
        {22, 0}, {-1, -3}, {-2, -21}, {-22, -23}, {-32, -32}
    },
    {
        {1, 2}, {3, 4}, {-12, 5}, {-11, -13}, {6, -14}, {-10, 7}, {8, -15}, {-9, 9}, {-16, 10}, {-8, -17},
        {11, 12}, {-7, -18}, {-6, 13}, {14, -5}, {15, -19}, {-4, 16}, {-20, 17}, {18, 19}, {20, 21},
        {22, 0}, {-1, -3}, {-2, -21}, {-22, -23}, {-32, -32}
    },
    {
        {1, 2}, {3, 4}, {-12, 5}, {-11, -13}, {6, -14}, {-10, 7}, {8, -15}, {-9, 9}, {-16, 10}, {-8, -17},
        {11, 12}, {-7, -18}, {-6, 13}, {14, -5}, {15, -19}, {-4, 16}, {-20, 17}, {18, 19}, {20, 21},
        {22, 0}, {-1, -3}, {-2, -21}, {-22, -23}, {-32, -32}
    },
    {
        {1, 2}, {3, 4}, {-12, 5}, {-11, -13}, {6, -14}, {-10, 7}, {8, -15}, {-9, 9}, {-16, 10}, {-8, -17},
        {11, 12}, {-7, -18}, {-6, 13}, {14, -5}, {15, -19}, {-4, 16}, {-20, 17}, {18, 19}, {20, 21},
        {22, 0}, {-1, -3}, {-2, -21}, {-22, -23}, {-32, -32}
    },
    {
        {1, 2}, {3, 4}, {-12, 5}, {-11, -13}, {6, -14}, {-10, 7}, {8, -15}, {-9, 9}, {-16, 10}, {-8, -17},
        {11, 12}, {-7, -18}, {-6, 13}, {14, -5}, {15, -19}, {-4, 16}, {-20, 17}, {18, 19}, {20, 21},
        {22, 0}, {-1, -3}, {-2, -21}, {-22, -23}, {-32, -32}
    },
    {
        {1, 2}, {3, 4}, {-12, 5}, {-11, -13}, {6, -14}, {-10, 7}, {8, -15}, {-9, 9}, {-16, 10}, {-8, -17},
        {11, 12}, {-7, -18}, {-6, 13}, {14, -5}, {15, -19}, {-4, 16}, {-20, 17}, {18, 19}, {20, 21},
        {22, 0}, {-1, -3}, {-2, -21}, {-22, -23}, {-32, -32}
    },
    {
        {1, 2}, {3, 4}, {-12, 5}, {-11, -13}, {6, -14}, {-10, 7}, {8, -15}, {-9, 9}, {-16, 10}, {-8, -17},
        {11, 12}, {-7, -18}, {-6, 13}, {14, -5}, {15, -19}, {-4, 16}, {-20, 17}, {18, 19}, {20, 21},
        {22, 0}, {-1, -3}, {-2, -21}, {-22, -23}, {-32, -32}
    },
    {
        {1, 2}, {3, 4}, {-12, 5}, {-11, -13}, {6, -14}, {-10, 7}, {8, -15}, {-9, 9}, {-16, 10}, {-8, -17},
        {11, 12}, {-7, -18}, {-6, 13}, {14, -5}, {15, -19}, {-4, 16}, {-20, 17}, {18, 19}, {20, 21},
        {22, 0}, {-1, -3}, {-2, -21}, {-22, -23}, {-32, -32}
    },
    {
        {1, 2}, {3, 4}, {-12, 5}, {-11, -13}, {6, -14}, {-10, 7}, {8, -15}, {-9, 9}, {-16, 10}, {-8, -17},
        {11, 12}, {-7, -18}, {-6, 13}, {14, -5}, {15, -19}, {-4, 16}, {-20, 17}, {18, 19}, {20, 21},
        {22, 0}, {-1, -3}, {-2, -21}, {-22, -23}, {-32, -32}
    },
    {
        {1, 2}, {3, 4}, {-12, 5}, {-11, -13}, {6, -14}, {-10, 7}, {8, -15}, {-9, 9}, {-16, 10}, {-8, -17},
        {11, 12}, {-7, -18}, {-6, 13}, {14, -5}, {15, -19}, {-4, 16}, {-20, 17}, {18, 19}, {20, 21},
        {22, 0}, {-1, -3}, {-2, -21}, {-22, -23}, {-32, -32}
    },
    {
        {1, 2}, {3, 4}, {-12, 5}, {-11, -13}, {6, -14}, {-10, 7}, {8, -15}, {-9, 9}, {-16, 10}, {-8, -17},
        {11, 12}, {-7, -18}, {-6, 13}, {14, -5}, {15, -19}, {-4, 16}, {-20, 17}, {18, 19}, {20, 21},
        {22, 0}, {-1, -3}, {-2, -21}, {-22, -23}, {-32, -32}
    },
    {
        {1, 2}, {3, 4}, {-12, 5}, {-11, -13}, {6, -14}, {-10, 7}, {8, -15}, {-9, 9}, {-16, 10}, {-8, -17},
        {11, 12}, {-7, -18}, {-6, 13}, {14, -5}, {15, -19}, {-4, 16}, {-20, 17}, {18, 19}, {20, 21},
        {22, 0}, {-1, -3}, {-2, -21}, {-22, -23}, {-32, -32}
    },
};

static const uint16_t decoder_tree0[360] = {
    2, 1, 4, 6, 8, 10, 12, 14, 16, 18, 33, 3, 20, 22, 24, 26, 28, 30, 32, 34, 36, 38, 35, 40,
    42, 44, 46, 5, 48, 65, 50, 52, 54, 56, 58, 60, 62, 64, 37, 66, 67, 68, 97, 70, 72, 74, 7,
    76, 78, 80, 82, 84, 86, 88, 99, 90, 39, 92, 94, 96, 129, 98, 9, 100, 102, 104, 106, 108,
    110, 112, 41, 161, 69, 114, 116, 118, 131, 120, 122, 11, 124, 126, 128, 193, 130, 132, 71,
    134, 43, 136, 138, 140, 163, 101, 13, 142, 144, 146, 148, 150, 152, 154, 225, 156, 158, 195,
    160, 162, 45, 164, 15, 166, 73, 168, 170, 133, 47, 172, 257, 174, 176, 178, 75, 103, 180, 165,
    182, 17, 227, 184, 105, 49, 135, 186, 289, 188, 259, 190, 192, 194, 196, 198, 291, 77, 200,
    202, 197, 107, 204, 19, 51, 229, 206, 167, 208, 210, 212, 214, 21, 79, 81, 109, 216, 218, 220,
    222, 53, 137, 224, 199, 226, 323, 321, 169, 228, 111, 230, 232, 139, 261, 234, 83, 236, 201,
    238, 240, 293, 242, 353, 231, 141, 244, 246, 113, 23, 355, 85, 248, 55, 115, 250, 263, 252,
    254, 203, 171, 256, 258, 233, 235, 143, 357, 325, 260, 295, 262, 173, 145, 177, 87, 264, 327,
    267, 266, 268, 175, 270, 272, 117, 297, 274, 265, 147, 179, 205, 276, 207, 237, 269, 278, 57,
    59, 387, 209, 280, 282, 149, 329, 385, 284, 25, 286, 239, 119, 288, 27, 290, 292, 299, 294, 359,
    89, 296, 298, 419, 181, 300, 331, 271, 417, 211, 361, 151, 389, 241, 302, 304, 303, 306, 308,
    421, 91, 310, 312, 391, 314, 121, 316, 333, 318, 275, 213, 301, 243, 183, 335, 320, 363, 322,
    215, 324, 393, 273, 337, 153, 326, 423, 365, 328, 367, 247, 395, 185, 123, 330, 425, 245, 155,
    332, 334, 305, 397, 336, 277, 217, 338, 340, 339, 427, 342, 344, 346, 307, 399, 187, 348, 309,
    341, 350, 369, 279, 311, 429, 249, 219, 352, 354, 356, 358, 431, 373, 401, 371, 313, 281, 433,
    343, 403, 251, 283
};

static const uint16_t decoder_tree1[188] = {
    2, 1, 4, 6, 8, 10, 12, 14, 16, 3, 33, 18, 20, 22, 24, 26, 35, 28, 30, 32, 34, 36, 5, 65, 38, 40,
    37, 42, 44, 46, 67, 48, 50, 52, 54, 56, 58, 60, 7, 62, 39, 97, 64, 69, 66, 99, 68, 70, 72, 74, 76,
    78, 80, 129, 41, 131, 82, 9, 71, 84, 86, 101, 88, 90, 92, 94, 96, 161, 43, 11, 73, 98, 103, 100,
    163, 102, 104, 106, 108, 133, 110, 105, 112, 75, 114, 45, 13, 116, 165, 118, 195, 135, 193, 120, 77,
    122, 47, 124, 167, 225, 126, 79, 107, 227, 128, 137, 197, 15, 130, 169, 199, 132, 109, 134, 17, 139,
    49, 136, 229, 138, 140, 81, 259, 142, 144, 171, 146, 141, 148, 111, 150, 201, 231, 152, 51, 257, 289,
    154, 19, 113, 156, 261, 158, 203, 173, 263, 143, 160, 291, 235, 83, 162, 233, 265, 164, 205, 166, 293,
    145, 168, 175, 177, 237, 115, 295, 170, 207, 172, 267, 174, 176, 297, 147, 178, 180, 269, 182, 271,
    209, 299, 239, 179, 184, 301, 241, 211, 0, 0
};

static const uint16_t decoder_tree2[96] = {
    2, 1, 4, 6, 8, 10, 12, 3, 17, 14, 19, 16, 18, 20, 22, 24, 26, 5, 21, 35, 33, 28, 30, 32, 34, 36, 38, 37,
    40, 23, 51, 42, 7, 49, 44, 46, 48, 50, 39, 53, 52, 54, 56, 25, 67, 9, 58, 60, 65, 55, 41, 62, 64, 69, 66,
    11, 27, 68, 57, 83, 70, 71, 81, 43, 72, 74, 13, 76, 85, 29, 73, 78, 99, 59, 87, 101, 80, 97, 45, 82, 84,
    75, 89, 61, 86, 103, 88, 77, 90, 105, 91, 92, 107, 93, 0, 0
};

static const uint16_t decoder_tree3[1040] = {
    2, 4, 6, 8, 10, 1, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32, 34, 3, 36, 1025, 38, 40, 42, 44, 46, 48, 50,
    129, 17, 52, 54, 1153, 19, 56, 58, 60, 62, 64, 66, 68, 145, 70, 72, 74, 76, 78, 1169, 1027, 147, 80, 82, 1171,
    84, 86, 131, 88, 1155, 1043, 1041, 90, 92, 5, 94, 96, 98, 100, 102, 104, 21, 106, 108, 2049, 2177, 110, 112, 114,
    116, 118, 120, 122, 124, 126, 128, 130, 132, 134, 136, 138, 140, 142, 33, 144, 163, 146, 148, 150, 152, 154, 161,
    156, 35, 158, 1297, 160, 162, 273, 257, 164, 166, 149, 168, 1281, 170, 172, 2193, 174, 176, 178, 1299, 180, 1045,
    182, 184, 1173, 186, 3201, 188, 190, 192, 194, 2195, 1187, 23, 2179, 196, 7, 198, 275, 200, 2051, 202, 2065, 204,
    206, 1029, 1185, 208, 210, 1157, 37, 3073, 2067, 133, 212, 214, 2321, 216, 165, 218, 1059, 220, 1283, 222, 2305,
    224, 226, 228, 230, 259, 232, 234, 2323, 236, 1409, 1057, 1315, 238, 240, 242, 244, 246, 1425, 248, 1313, 250, 252,
    254, 256, 258, 260, 289, 262, 264, 1189, 266, 268, 179, 151, 270, 272, 274, 276, 278, 291, 280, 282, 9, 385, 284,
    286, 177, 49, 401, 1061, 288, 290, 292, 51, 294, 296, 298, 300, 302, 304, 25, 306, 2083, 39, 308, 310, 3329, 167,
    312, 314, 1175, 316, 318, 1203, 135, 320, 322, 324, 326, 328, 2211, 2307, 330, 1301, 332, 334, 1047, 336, 338, 2449,
    3217, 340, 1427, 2209, 53, 342, 2339, 3345, 344, 346, 348, 403, 181, 4097, 2197, 350, 2181, 1285, 1317, 1031, 352,
    354, 356, 3089, 358, 360, 4225, 277, 362, 364, 366, 368, 2069, 370, 3203, 293, 1201, 305, 372, 3219, 307, 2433, 374,
    376, 378, 380, 2081, 1411, 382, 384, 3075, 1443, 513, 386, 387, 388, 390, 1331, 261, 392, 394, 396, 398, 400, 1441,
    1075, 67, 1159, 402, 404, 406, 408, 410, 412, 414, 3347, 2325, 416, 65, 418, 420, 422, 424, 426, 2053, 193, 1073, 428,
    430, 432, 1537, 1329, 2337, 2213, 434, 417, 183, 41, 436, 438, 440, 442, 444, 446, 448, 450, 195, 2435, 452, 2085, 1063,
    1191, 454, 456, 458, 460, 419, 2071, 1553, 3091, 55, 137, 462, 464, 466, 468, 470, 472, 474, 476, 478, 2309, 4113, 480,
    482, 484, 486, 2451, 2465, 1205, 153, 488, 490, 492, 494, 496, 498, 500, 502, 504, 506, 508, 510, 512, 514, 516, 518,
    520, 522, 524, 1333, 526, 1555, 2467, 2227, 3205, 3331, 528, 530, 532, 534, 536, 538, 540, 542, 544, 546, 548, 529, 309,
    1303, 3473, 3457, 389, 1569, 1445, 1077, 69, 2199, 1539, 4353, 550, 552, 554, 556, 558, 560, 562, 1459, 4241, 3221, 1429,
    2341, 279, 3475, 169, 564, 545, 3105, 323, 2353, 2097, 3235, 421, 2229, 3107, 3233, 566, 568, 570, 572, 574, 576, 578,
    580, 582, 584, 586, 588, 590, 592, 594, 596, 2099, 1091, 531, 2437, 4227, 405, 197, 263, 1287, 2577, 1049, 1571, 598, 600,
    602, 604, 606, 608, 610, 612, 614, 616, 618, 620, 622, 624, 626, 628, 630, 632, 634, 636, 638, 640, 642, 644, 646, 648, 650,
    1345, 1219, 3077, 1457, 2225, 2579, 515, 2561, 2469, 433, 1221, 2183, 4243, 652, 654, 656, 658, 660, 662, 664, 666, 668,
    670, 1217, 3333, 3093, 435, 321, 4369, 1089, 2055, 4099, 3361, 1319, 547, 1161, 1177, 672, 2355, 4115, 1413, 4257, 3349,
    2453, 3109, 2357, 2215, 3363, 1079, 1207, 311, 1033, 1347, 1065, 674, 676, 678, 680, 682, 684, 686, 688, 690, 692, 694, 696,
    698, 700, 702, 704, 706, 708, 710, 712, 714, 716, 718, 720, 722, 724, 726, 728, 730, 732, 734, 736, 738, 740, 742, 744, 746,
    748, 750, 752, 754, 756, 758, 760, 762, 764, 766, 768, 770, 772, 774, 776, 778, 780, 782, 784, 786, 788, 790, 792, 794, 796,
    798, 800, 802, 804, 806, 808, 810, 812, 814, 2593, 2565, 4261, 3253, 437, 325, 3489, 2311, 4259, 1431, 2087, 2563, 295, 2343,
    449, 199, 265, 2201, 4371, 1193, 816, 533, 1557, 2581, 2241, 3365, 3491, 3603, 549, 2101, 1461, 1093, 2117, 3459, 3079, 4481,
    3095, 2327, 3461, 4129, 3249, 1447, 2471, 2231, 71, 4497, 2609, 1289, 393, 3251, 2073, 3097, 2371, 1305, 2089, 818, 820, 822,
    824, 826, 828, 830, 832, 834, 836, 838, 840, 842, 844, 846, 848, 850, 852, 854, 856, 858, 860, 862, 864, 866, 868, 870, 872,
    874, 876, 878, 880, 882, 884, 886, 888, 890, 892, 894, 896, 898, 900, 902, 904, 906, 908, 910, 912, 914, 916, 918, 920, 922,
    924, 926, 928, 930, 932, 934, 936, 938, 940, 942, 944, 946, 948, 950, 952, 954, 956, 958, 960, 962, 964, 966, 968, 970, 972,
    974, 976, 978, 980, 982, 984, 986, 988, 990, 992, 994, 996, 998, 1000, 1002, 1004, 1006, 1008, 1010, 1012, 1014, 1016, 1018,
    1020, 1022, 1024, 1026, 1028, 1030, 1032, 1034, 1036, 4161, 4273, 3507, 3493, 4517, 2497, 1573, 2597, 3621, 4531, 4627, 3523,
    3125, 4149, 4529, 3139, 4515, 451, 4277, 2113, 4163, 4499, 3381, 4405, 1473, 4373, 2485, 3509, 565, 1589, 2613, 3585, 3123,
    4403, 3141, 4147, 563, 2245, 3269, 4357, 1349, 2373, 3397, 453, 1477, 2501, 2481, 579, 1601, 3477, 4103, 3265, 2243, 1587,
    3207, 4231, 3267, 4501, 1475, 3335, 4359, 391, 1415, 2439, 3463, 4487, 519, 1543, 2567, 3591, 4609, 4289, 4611, 2499, 4119,
    4385, 4145, 4401, 3223, 4247, 3379, 577, 3393, 3351, 4375, 407, 1585, 2455, 3479, 4503, 535, 1559, 2583, 3607, 3605, 4513,
    4485, 3111, 4135, 3121, 517, 3377, 3239, 4263, 1541, 4291, 4229, 3367, 4391, 423, 2115, 4131, 3495, 551, 1575, 2599, 3635, 3395,
    2103, 3127, 4151, 3589, 4101, 1603, 3255, 4279, 3601, 1335, 2359, 3383, 439, 1463, 2487, 3511, 567, 1591, 4133, 1095, 2119, 3143,
    2369, 1223, 2247, 3271, 327, 1351, 2375, 455, 1479, 3137, 3521, 2057, 3081, 4105, 4387, 3505, 2185, 3209, 4233, 3587, 4355, 2313,
    3337, 3237, 1417, 2441, 3465, 521, 1545, 3617, 3633, 561, 4625, 4121, 2611, 2483, 2595, 3225, 4249, 281, 4245, 2329, 3353, 409,
    1433, 2457, 3481, 537, 1561, 4483, 3619, 4389, 3113, 4275, 4117, 2217, 3241, 297, 1321, 2345, 3369, 425, 1449, 2473, 57, 1081,
    2105, 3129, 185, 1209, 2233, 3257, 313, 1337, 2361, 441, 1465, 73, 1097, 201, 1225, 0, 0
};

static const uint16_t decoder_tree4[416] = {
    2, 4, 6, 1, 8, 10, 12, 14, 16, 18, 20, 22, 24, 3, 129, 26, 28, 9, 33, 30, 32,
    34, 36, 11, 161, 38, 40, 42, 41, 44, 46, 131, 43, 169, 35, 48, 137, 50, 52, 54, 56, 139,
    163, 171, 58, 60, 62, 64, 5, 66, 68, 70, 257, 72, 74, 76, 13, 78, 80, 289, 82, 84, 17,
    86, 88, 65, 90, 201, 19, 92, 94, 51, 193, 96, 98, 49, 100, 73, 102, 104, 106, 45, 108, 110,
    297, 112, 114, 116, 37, 203, 118, 120, 179, 122, 177, 124, 265, 126, 75, 133, 259, 291, 147, 128, 67,
    195, 130, 141, 173, 299, 132, 145, 134, 165, 136, 138, 140, 142, 7, 144, 146, 21, 267, 148, 53, 150,
    321, 152, 154, 15, 156, 81, 158, 160, 385, 162, 417, 164, 166, 168, 83, 170, 172, 329, 174, 211, 176,
    27, 178, 180, 182, 209, 184, 186, 188, 190, 25, 192, 331, 194, 196, 105, 57, 198, 97, 200, 202, 323,
    225, 59, 149, 204, 206, 233, 307, 208, 77, 181, 210, 212, 214, 216, 218, 220, 222, 47, 224, 226, 69,
    228, 230, 197, 232, 425, 393, 205, 275, 293, 39, 234, 236, 238, 305, 135, 155, 301, 143, 240, 242, 235,
    395, 244, 246, 248, 250, 252, 254, 256, 258, 260, 262, 273, 269, 185, 264, 266, 268, 270, 272, 274, 276,
    261, 153, 278, 280, 282, 187, 337, 387, 107, 284, 427, 227, 167, 419, 286, 288, 290, 292, 294, 296, 298,
    300, 302, 304, 306, 308, 310, 312, 314, 316, 318, 320, 322, 324, 326, 328, 330, 332, 334, 336, 338, 115,
    99, 85, 213, 29, 113, 23, 89, 241, 61, 449, 339, 175, 340, 342, 344, 346, 348, 350, 352, 354, 356,
    358, 360, 362, 364, 366, 368, 370, 372, 374, 376, 378, 380, 382, 384, 386, 388, 390, 392, 394, 396, 398,
    400, 402, 404, 406, 408, 410, 412, 414, 389, 361, 457, 465, 429, 451, 333, 109, 277, 243, 263, 295, 199,
    283, 151, 55, 183, 229, 357, 363, 123, 491, 397, 411, 251, 313, 441, 467, 345, 433, 461, 219, 237, 365,
    435, 353, 347, 405, 409, 217, 309, 437, 369, 371, 341, 117, 245, 249, 157, 285, 403, 189, 317, 93, 221,
    315, 401, 481, 391, 489, 121, 421, 423, 71, 483, 327, 103, 231, 443, 459, 271, 399, 355, 91, 303, 431,
    79, 207, 335, 111, 239, 281, 325, 279, 453, 101, 311, 87, 215, 31, 159, 63, 191
};

static const uint16_t decoder_tree5[384] = {
    2, 4, 1, 6, 8, 10, 12, 14, 16, 18, 20, 22, 3, 513, 24, 26, 28, 9, 129, 33, 30, 32, 34, 36, 38, 40, 11, 42, 641, 44, 46, 41,
    161, 48, 515, 50, 52, 131, 54, 35, 545, 137, 56, 58, 60, 521, 62, 43, 673, 64, 169, 66, 68, 523, 70, 163, 643, 139, 553, 72, 649, 74, 547,
    76, 78, 80, 681, 171, 82, 84, 555, 86, 675, 88, 651, 5, 90, 92, 1025, 94, 96, 98, 683, 13,
    100, 17, 102, 104, 106, 65, 108, 110, 257, 112, 114, 1153, 19, 116, 118, 120, 122, 124, 49, 126, 128,
    769, 289, 130, 132, 134, 73, 136, 138, 140, 142, 193, 144, 146, 148, 150, 152, 154, 517, 156, 158, 37,
    51, 160, 201, 162, 145, 164, 166, 168, 133, 170, 801, 45, 172, 174, 1057, 176, 178, 67, 180, 1027, 577,
    182, 184, 186, 188, 190, 192, 194, 196, 198, 259, 200, 202, 204, 525, 177, 265, 141, 206, 208, 210, 212,
    195, 297, 214, 75, 216, 1033, 203, 585, 1155, 1185, 267, 1161, 549, 218, 220, 657, 777, 147, 222, 224, 226,
    228, 230, 232, 234, 236, 238, 240, 587, 645, 165, 242, 244, 246, 248, 250, 771, 291, 252, 579, 1065, 1035,
    705, 531, 529, 659, 173, 254, 561, 653, 256, 713, 677, 557, 258, 260, 262, 264, 266, 268, 270, 272, 274,
    276, 278, 280, 282, 284, 286, 288, 290, 292, 294, 296, 298, 300, 707, 1059, 809, 715, 563, 179, 691, 1193,
    21, 779, 1067, 299, 1187, 302, 304, 306, 308, 310, 312, 314, 316, 318, 320, 322, 324, 326, 328, 330, 332,
    334, 336, 338, 340, 342, 344, 346, 348, 350, 352, 354, 356, 358, 360, 362, 364, 366, 368, 370, 372, 374,
    376, 378, 380, 83, 69, 1281, 803, 321, 1195, 1163, 811, 1323, 689, 1321, 1099, 305, 835, 1227, 331, 843, 785,
    593, 1043, 1291, 1283, 1171, 275, 787, 1217, 833, 1075, 1313, 1219, 1203, 307, 819, 841, 595, 211, 723, 721, 817,
    1029, 329, 81, 1157, 261, 773, 1097, 1089, 1061, 1169, 1091, 1189, 293, 805, 1201, 581, 197, 709, 1289, 273, 1037,
    1315, 1041, 1165, 269, 781, 209, 1073, 1069, 323, 685, 1197, 301, 813, 77, 589, 205, 717, 1225, 533, 149, 661,
    53, 565, 181, 693, 0, 0
};

static const uint16_t decoder_tree6[62] = {
    2, 1, 4, 6, 8, 10, 12, 14, 16, 3, 33, 5, 17, 9, 18, 20, 22, 24, 26, 28, 30, 32, 34, 7, 49, 13, 25, 36, 38, 11,
    21, 41, 35, 37, 19, 40, 42, 44, 46, 48, 50, 15, 52, 57, 29, 27, 23, 53, 54, 51, 39, 45, 43, 56, 58, 31, 55, 60,
    61, 47, 59, 63
};

static const uint16_t *const decoder_tables[7] = {
    decoder_tree0,
    decoder_tree1,
    decoder_tree2,
    decoder_tree3,
    decoder_tree4,
    decoder_tree5,
    decoder_tree6,
};

static const int decoder_tables_elements[7] = {
    FF_ARRAY_ELEMS(decoder_tree0),
    FF_ARRAY_ELEMS(decoder_tree1),
    FF_ARRAY_ELEMS(decoder_tree2),
    FF_ARRAY_ELEMS(decoder_tree3),
    FF_ARRAY_ELEMS(decoder_tree4),
    FF_ARRAY_ELEMS(decoder_tree5),
    FF_ARRAY_ELEMS(decoder_tree6),
};

static const float mlt_quant[7][14] = {
    { 0.0f, 0.392f, 0.761f, 1.120f, 1.477f, 1.832f, 2.183f, 2.541f, 2.893f, 3.245f, 3.598f, 3.942f, 4.288f, 4.724f },
    { 0.0f, 0.544f, 1.060f, 1.563f, 2.068f, 2.571f, 3.072f, 3.562f, 4.070f, 4.620f, 0.0f, 0.0f, 0.0f, 0.0f },
    { 0.0f, 0.746f, 1.464f, 2.180f, 2.882f, 3.584f, 4.316f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    { 0.0f, 1.006f, 2.000f, 2.993f, 3.985f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    { 0.0f, 1.321f, 2.703f, 3.983f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    { 0.0f, 1.657f, 3.491f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    { 0.0f, 1.964f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }
};

static const float noise_category5[21] = {
    0.70711f, 0.6179f, 0.5005f, 0.3220f, 0.17678f, 0.17678f, 0.17678f, 0.17678f, 0.17678f, 0.17678f, 0.17678f,
    0.17678f, 0.17678f, 0.17678f, 0.17678f, 0.17678f, 0.17678f, 0.17678f, 0.17678f, 0.17678f
};

static const float noise_category6[21] = {
    0.70711f, 0.5686f, 0.3563f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f,
    0.25f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f
};

#define FRAME_SIZE 320
#define REGION_SIZE 20

typedef struct SirenContext {
    GetBitContext gb;

    int microsoft;
    int rate_control_possibilities;
    int esf_adjustment;
    int number_of_regions;
    int scale_factor;
    int sample_rate_bits;
    int checksum_bits;

    unsigned dw1, dw2, dw3, dw4;

    int absolute_region_power_index[32];
    float decoder_standard_deviation[32];
    int power_categories[32];
    int category_balance[32];
    float standard_deviation[64];
    float backup_frame[FRAME_SIZE];

    AVFloatDSPContext *fdsp;
    av_tx_fn           tx_fn;
    AVTXContext       *tx_ctx;

    DECLARE_ALIGNED(32, float, imdct_buf)[4][FRAME_SIZE];
    float          *window;
    float          *imdct_in;
    float          *imdct_out;
    float          *imdct_prev;
} SirenContext;

static av_cold int siren_init(AVCodecContext *avctx)
{
    const float scale = 1.0f / (22.f * 32768.f);
    SirenContext *s = avctx->priv_data;
    int i;

    s->imdct_in   = s->imdct_buf[0];
    s->imdct_out  = s->imdct_buf[1];
    s->imdct_prev = s->imdct_buf[2];
    s->window     = s->imdct_buf[3];

    av_channel_layout_uninit(&avctx->ch_layout);
    avctx->ch_layout      = (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;
    avctx->sample_fmt     = AV_SAMPLE_FMT_FLT;

    s->rate_control_possibilities = 16;
    s->esf_adjustment = 7;
    s->number_of_regions = 14;
    s->scale_factor = 22;
    s->dw1 = s->dw2 = s->dw3 = s->dw4 = 1;

    for (i = 0; i < 64; i++) {
        float region_power = powf(10, (i - 24) * 0.3010299957);

        s->standard_deviation[i] = sqrtf(region_power);
    }

    for (i = 0; i < FRAME_SIZE; i++) {
        float angle = ((i + 0.5f) * M_PI_2) / 320.f;
        s->window[i] = sinf(angle);
    }

    s->fdsp = avpriv_float_dsp_alloc(avctx->flags & AV_CODEC_FLAG_BITEXACT);
    if (!s->fdsp)
        return AVERROR(ENOMEM);

    s->microsoft = avctx->codec->id == AV_CODEC_ID_MSNSIREN;
    if (s->microsoft) {
        s->esf_adjustment = -2;
        s->number_of_regions = 14;
        s->scale_factor = 1;
        s->sample_rate_bits = 2;
        s->checksum_bits = 4;
    }

    return av_tx_init(&s->tx_ctx, &s->tx_fn, AV_TX_FLOAT_MDCT, 1, FRAME_SIZE, &scale, 0);
}

static int decode_envelope(SirenContext *s, GetBitContext *gb,
                           int number_of_regions, float *decoder_standard_deviation,
                           int *absolute_region_power_index, int esf_adjustment)
{
    absolute_region_power_index[0] = (int)get_bits(gb, 5) - esf_adjustment;
    absolute_region_power_index[0] = av_clip(absolute_region_power_index[0], -24, 39);
    decoder_standard_deviation[0] = s->standard_deviation[absolute_region_power_index[0] + 24];

    for (int i = 1; i < number_of_regions; i++) {
        int index = 0;

        do {
            if (get_bits_left(gb) < 4 + number_of_regions - i + s->checksum_bits)
                return AVERROR_INVALIDDATA;
            index = differential_decoder_tree[i - 1][index][get_bits1(gb)];
        } while (index > 0);

        absolute_region_power_index[i] = av_clip(absolute_region_power_index[i - 1] - index - 12, -24, 39);
        decoder_standard_deviation[i] = s->standard_deviation[absolute_region_power_index[i] + 24];
    }

    return get_bits_count(gb);
}

static int categorize_regions(int number_of_regions, int number_of_available_bits,
                              int *absolute_region_power_index, int *power_categories,
                              int *category_balance)
{
    int region, delta, i, temp;
    int expected_number_of_code_bits;
    int min, max;
    int offset, num_rate_control_possibilities = 16,
        raw_value, raw_max_idx = 0, raw_min_idx = 0;
    int max_rate_categories[28];
    int min_rate_categories[28];
    int temp_category_balances[64];
    int *min_rate_ptr = NULL;
    int *max_rate_ptr = NULL;

    offset = -32;
    for (delta = 32; number_of_regions > 0 && delta > 0; delta /= 2) {
        expected_number_of_code_bits = 0;
        for (region = 0; region < number_of_regions; region++) {
            i = (delta + offset -
                 absolute_region_power_index[region]) >> 1;
            i = av_clip_uintp2(i, 3);
            power_categories[region] = i;
            expected_number_of_code_bits += expected_bits_table[i];

        }
        if (expected_number_of_code_bits >= number_of_available_bits - 32)
            offset += delta;
    }

    expected_number_of_code_bits = 0;
    for (region = 0; region < number_of_regions; region++) {
        i = (offset - absolute_region_power_index[region]) >> 1;
        i = av_clip_uintp2(i, 3);
        max_rate_categories[region] = min_rate_categories[region] =
            power_categories[region] = i;
        expected_number_of_code_bits += expected_bits_table[i];
    }

    min = max = expected_number_of_code_bits;
    min_rate_ptr = max_rate_ptr =
        temp_category_balances + num_rate_control_possibilities;
    for (i = 0; i < num_rate_control_possibilities - 1; i++) {
        if (min + max > number_of_available_bits * 2) {
            raw_value = -99;
            for (region = number_of_regions - 1; region >= 0; region--) {
                if (min_rate_categories[region] < 7) {
                    temp =
                        offset - absolute_region_power_index[region] -
                        2 * min_rate_categories[region];
                    if (temp > raw_value) {
                        raw_value = temp;
                        raw_min_idx = region;
                    }
                }
            }
            if (raw_value == -99)
                return AVERROR_INVALIDDATA;
            *min_rate_ptr++ = raw_min_idx;
            min +=
                expected_bits_table[min_rate_categories[raw_min_idx] + 1] -
                expected_bits_table[min_rate_categories[raw_min_idx]];
            min_rate_categories[raw_min_idx]++;
        } else {
            raw_value = 99;
            for (region = 0; region < number_of_regions; region++) {
                if (max_rate_categories[region] > 0) {
                    temp =
                        offset - absolute_region_power_index[region] -
                        2 * max_rate_categories[region];
                    if (temp < raw_value) {
                        raw_value = temp;
                        raw_max_idx = region;
                    }
                }
            }
            if (raw_value == 99)
                return AVERROR_INVALIDDATA;

            *--max_rate_ptr = raw_max_idx;
            max += expected_bits_table[max_rate_categories[raw_max_idx] - 1] -
                   expected_bits_table[max_rate_categories[raw_max_idx]];
            max_rate_categories[raw_max_idx]--;
        }
    }

    for (region = 0; region < number_of_regions; region++)
        power_categories[region] = max_rate_categories[region];

    for (i = 0; i < num_rate_control_possibilities - 1; i++)
        category_balance[i] = *max_rate_ptr++;

    return 0;
}

static int get_dw(SirenContext *s)
{
    int ret = s->dw1 + s->dw4;

    if ((ret & 0x8000) != 0)
        ret++;

    s->dw1 = s->dw2;
    s->dw2 = s->dw3;
    s->dw3 = s->dw4;
    s->dw4 = ret;

    return ret;
}

static int decode_vector(SirenContext *s, int number_of_regions,
                         float *decoder_standard_deviation,
                         int *power_categories, float *coefs, int scale_factor)
{
    GetBitContext *gb = &s->gb;
    float *coefs_ptr;
    float decoded_value;
    float noise;
    const uint16_t *decoder_tree;
    int region;
    int category;
    int i, j;
    int index;
    int error = 0;
    int dw1;
    int dw2;

    for (region = 0; region < number_of_regions; region++) {
        category = power_categories[region];
        coefs_ptr = coefs + (region * REGION_SIZE);

        if (category >= 0 && category < 7) {
            decoder_tree = decoder_tables[category];

            for (i = 0; i < number_of_vectors[category]; i++) {
                index = 0;
                do {
                    if (get_bits_left(gb) - s->checksum_bits <= 0) {
                        error = 1;
                        break;
                    }

                    if (index + show_bits1(gb) >= decoder_tables_elements[category]) {
                        error = 1;
                        break;
                    }
                    index = decoder_tree[index + get_bits1(gb)];
                } while ((index & 1) == 0);

                index >>= 1;

                if (error == 0) {
                    for (j = 0; j < vector_dimension[category]; j++) {
                        decoded_value = mlt_quant[category][index & ((1 << index_table[category]) - 1)];
                        index >>= index_table[category];

                        if (decoded_value) {
                            if (get_bits_left(gb) - s->checksum_bits <= 0) {
                                error = 1;
                                break;
                            }
                            if (!get_bits1(gb))
                                decoded_value *= -decoder_standard_deviation[region];
                            else
                                decoded_value *= decoder_standard_deviation[region];
                        }

                        *coefs_ptr++ = decoded_value * scale_factor;
                    }
                } else {
                    error = 1;
                    break;
                }
            }

            if (error == 1) {
                for (j = region + 1; j < number_of_regions; j++)
                    power_categories[j] = 7;
                category = 7;
            }
        }

        coefs_ptr = coefs + (region * REGION_SIZE);

        if (category == 5 && s->microsoft) {
            i = 0;
            for (j = 0; j < REGION_SIZE; j++) {
                if (*coefs_ptr != 0) {
                    i++;
                    if (fabs(*coefs_ptr) > 2.0 * decoder_standard_deviation[region]) {
                        i += 3;
                    }
                }
                coefs_ptr++;
            }
            if (i >= FF_ARRAY_ELEMS(noise_category5)) {
                error = 1;
                break;
            }

            noise = decoder_standard_deviation[region] * noise_category5[i];
        } else
        if (category == 5 || category == 6) {
            i = 0;
            for (j = 0; j < REGION_SIZE; j++) {
                if (*coefs_ptr != 0)
                    i++;
                coefs_ptr++;
            }

            if (category == 5) {
                noise = decoder_standard_deviation[region] * noise_category5[i];
            } else
                noise = decoder_standard_deviation[region] * noise_category6[i];
        } else if (category == 7) {
            noise = decoder_standard_deviation[region] * 0.70711f;
        } else {
            noise = 0;
        }

        coefs_ptr = coefs + (region * REGION_SIZE);

        if (category == 5 || category == 6 || category == 7) {
            dw1 = get_dw(s);
            dw2 = get_dw(s);

            for (j = 0; j < 10; j++) {
                if (category == 7 || *coefs_ptr == 0)
                    *coefs_ptr = dw1 & 1 ? noise : -noise;
                coefs_ptr++;
                dw1 >>= 1;

                if (category == 7 || *coefs_ptr == 0)
                    *coefs_ptr = dw2 & 1 ? noise : -noise;
                coefs_ptr++;
                dw2 >>= 1;
            }
        }
    }

    return error == 1 ? AVERROR_INVALIDDATA : (get_bits_left(gb) - s->checksum_bits);
}

static int siren_decode(AVCodecContext *avctx, AVFrame *frame,
                        int *got_frame, AVPacket *avpkt)
{
    SirenContext *s = avctx->priv_data;
    GetBitContext *gb = &s->gb;
    int ret, number_of_valid_coefs = REGION_SIZE * s->number_of_regions;
    int frame_error = 0, rate_control = 0;
    int bits_per_frame;

    if (s->microsoft) {
        bits_per_frame  = avctx->sample_rate / 50;

        if (avpkt->size < bits_per_frame / 8)
            return AVERROR_INVALIDDATA;

        if ((ret = init_get_bits(gb, avpkt->data, bits_per_frame)) < 0)
            return ret;
    } else
        if ((ret = init_get_bits8(gb, avpkt->data, avpkt->size)) < 0)
            return ret;

    skip_bits(gb, s->sample_rate_bits);

    ret = decode_envelope(s, gb, s->number_of_regions,
                    s->decoder_standard_deviation,
                    s->absolute_region_power_index, s->esf_adjustment);
    if (ret < 0)
        return ret;

    rate_control = get_bits(gb, 4);

    ret = categorize_regions(s->number_of_regions, get_bits_left(gb) - s->checksum_bits,
                             s->absolute_region_power_index, s->power_categories,
                             s->category_balance);
    if (ret < 0)
        return ret;

    for (int i = 0; i < rate_control; i++)
        s->power_categories[s->category_balance[i]]++;

    ret = decode_vector(s, s->number_of_regions,
                        s->decoder_standard_deviation, s->power_categories,
                        s->imdct_in, s->scale_factor);
    if (ret < 0 && !s->microsoft)
        return ret;

    if (get_bits_left(gb) - s->checksum_bits > 0) {
        do {
            frame_error |= !get_bits1(gb);
        } while (get_bits_left(gb) - s->checksum_bits > 0);
    } else if (get_bits_left(gb) - s->checksum_bits < 0 &&
               rate_control + 1 < s->rate_control_possibilities) {
        frame_error = 1;
    }

    for (int i = 0; i < s->number_of_regions; i++) {
        if (s->absolute_region_power_index[i] > 33 ||
            s->absolute_region_power_index[i] < -31)
            frame_error = 1;
    }

    if ((avctx->err_recognition & AV_EF_CRCCHECK) && s->checksum_bits) {
        static const uint16_t ChecksumTable[4] = {0x7F80, 0x7878, 0x6666, 0x5555};
        int wpf, checksum, sum, calculated_checksum, temp1;

        checksum = get_bits(gb, s->checksum_bits);

        wpf = bits_per_frame / 16;
        sum = 0;
        for (int i = 0; i < wpf - 1; i++)
            sum ^= AV_RB16(avpkt->data + i * 2) << (i % 15);
        sum ^= (AV_RB16(avpkt->data + (wpf - 1) * 2) & ~checksum) << ((wpf - 1) % 15);
        sum = (sum >> 15) ^ (sum & 0x7FFF);

        calculated_checksum = 0;
        for (int i = 0; i < 4; i++) {
            temp1 = ChecksumTable[i] & sum;

            for (int j = 8; j > 0; j >>= 1)
                temp1 ^= temp1 >> j;

            calculated_checksum <<= 1;
            calculated_checksum |= temp1 & 1;
        }

        if (checksum != calculated_checksum) {
            av_log(avctx, AV_LOG_WARNING, "Invalid checksum\n");
            if (avctx->err_recognition & AV_EF_EXPLODE)
                return AVERROR_INVALIDDATA;
            frame_error = 1;
        }
    }

    if (frame_error) {
        memcpy(s->imdct_in, s->backup_frame, number_of_valid_coefs * sizeof(float));
        memset(s->backup_frame, 0, number_of_valid_coefs * sizeof(float));
    } else {
        memcpy(s->backup_frame, s->imdct_in, number_of_valid_coefs * sizeof(float));
    }

    frame->nb_samples = FRAME_SIZE;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    for (int i = 0; i < FRAME_SIZE; i += 2)
        s->imdct_in[i] *= -1;

    s->tx_fn(s->tx_ctx, s->imdct_out, s->imdct_in, sizeof(float));
    s->fdsp->vector_fmul_window((float *)frame->data[0],
                                s->imdct_prev + (FRAME_SIZE >> 1),
                                s->imdct_out, s->window,
                                FRAME_SIZE >> 1);
    FFSWAP(float *, s->imdct_out, s->imdct_prev);

    *got_frame = 1;

    return s->microsoft ? bits_per_frame / 8 : avpkt->size;
}

static av_cold void siren_flush(AVCodecContext *avctx)
{
    SirenContext *s = avctx->priv_data;

    memset(s->backup_frame, 0, sizeof(s->backup_frame));
    memset(s->imdct_prev, 0, FRAME_SIZE * sizeof(*s->imdct_prev));
    memset(s->imdct_out, 0, FRAME_SIZE * sizeof(*s->imdct_out));
}

static av_cold int siren_close(AVCodecContext *avctx)
{
    SirenContext *s = avctx->priv_data;

    av_freep(&s->fdsp);
    av_tx_uninit(&s->tx_ctx);

    return 0;
}

const FFCodec ff_siren_decoder = {
    .p.name         = "siren",
    CODEC_LONG_NAME("Siren"),
    .priv_data_size = sizeof(SirenContext),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_SIREN,
    .init           = siren_init,
    .close          = siren_close,
    FF_CODEC_DECODE_CB(siren_decode),
    .flush          = siren_flush,
    .p.capabilities = AV_CODEC_CAP_CHANNEL_CONF |
                      AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
};

const FFCodec ff_msnsiren_decoder = {
    .p.name         = "msnsiren",
    CODEC_LONG_NAME("MSN Siren"),
    .priv_data_size = sizeof(SirenContext),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_MSNSIREN,
    .init           = siren_init,
    .close          = siren_close,
    FF_CODEC_DECODE_CB(siren_decode),
    .flush          = siren_flush,
    .p.capabilities = AV_CODEC_CAP_CHANNEL_CONF |
                      AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
};
