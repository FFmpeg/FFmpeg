/*
 * Copyright (c) 2015 Kieran Kunhya
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

#include <stdint.h>

#include "libavutil/attributes.h"

#include "cfhd.h"
#include "vlc.h"

#define NB_VLC_TABLE_9   (71 + 3)
#define NB_VLC_TABLE_18 (263 + 1)

typedef struct CFHD_RL_ELEM {
    uint16_t run;
    uint8_t level;
    uint8_t len;
} CFHD_RL_ELEM;

static const CFHD_RL_ELEM table_9_vlc[NB_VLC_TABLE_9] = {
    {   1,   0,  1 }, {   1,   1,  2 }, {   1,   2,  4 }, {   1,   3,  5 },
    {   1,   6,  7 }, {   1,  15, 11 }, {   1,  26, 14 }, {   1,  27, 14 },
    {   1,  23, 13 }, {   1,  36, 16 }, {   1,  40, 17 }, {   1,  41, 17 },
    {   1,  32, 15 }, {   1,  48, 19 }, {   1,  49, 19 }, {   1,  51, 20 },
    {   1,  52, 20 }, {   1,  50, 19 }, {   1,  42, 17 }, {   1,  37, 16 },
    {   1,  33, 15 }, {   1,  24, 13 }, {   1,  13, 10 }, {   1,  10,  9 },
    {   1,   8,  8 }, {   1,   5,  6 }, {  80,   0,  8 }, {   1,  16, 11 },
    {   1,  19, 12 }, {   1,  20, 12 }, {   1,  14, 10 }, { 120,   0,  9 },
    { 320,   0,  8 }, {   1,  11,  9 }, {   1,  28, 14 }, {   1,  29, 14 },
    {   1,  25, 13 }, {   1,  21, 12 }, {   1,  17, 11 }, {   1,  34, 15 },
    {   1,  45, 18 }, {   1,  46, 18 }, {   1,  47, 18 }, {   1,  57, 23 },
    {   1,  58, 23 }, {   1,  59, 23 }, {   1,  62, 24 }, {   1,  63, 24 },
    {   1,  56, 22 }, {   1,  60, 23 }, {   1,  61, 24 }, {   1,  64, 25 },
    {   0,  64, 26 }, {   1,  64, 26 }, {   1,  53, 20 }, {   1,  54, 20 },
    {   1,  55, 20 }, {   1,  38, 16 }, {   1,  30, 14 }, {   1,  31, 14 },
    {   1,  43, 17 }, {   1,  44, 17 }, {   1,  39, 16 }, {   1,  35, 15 },
    {   1,  22, 12 }, {   1,  18, 11 }, {  32,   0,  6 }, {  12,   0,  5 },
    {   1,   4,  5 }, { 160,   0,  6 }, {   1,   7,  7 }, {   1,   9,  8 },
    { 100,   0,  9 }, {   1,  12,  9 },
};

static const CFHD_RL_ELEM table_18_vlc[NB_VLC_TABLE_18] = {
    {   1,   0,  1 }, {   1,   1,  2 }, {   1,   4,  6 }, {   1,  14, 10 },
    {   1,  23, 12 }, {   1,  41, 15 }, {   1,  54, 17 }, {   1,  69, 19 },
    {   1,  77, 20 }, {   1,  85, 21 }, {   1, 108, 24 }, {   1, 237, 25 },
    {   1, 238, 25 }, {   1, 101, 23 }, {   1, 172, 25 }, {   1, 173, 25 },
    {   1, 170, 25 }, {   1, 171, 25 }, {   1, 227, 25 }, {   1, 162, 25 },
    {   1, 156, 25 }, {   1, 157, 25 }, {   1, 243, 25 }, {   1, 134, 25 },
    {   1, 135, 25 }, {   1, 136, 25 }, {   1, 128, 25 }, {   1, 129, 25 },
    {   1, 247, 25 }, {   1, 127, 25 }, {   1, 178, 25 }, {   1, 179, 25 },
    {   1, 214, 25 }, {   1, 118, 25 }, {   1, 204, 25 }, {   1, 205, 25 },
    {   1, 198, 25 }, {   1, 199, 25 }, {   1, 245, 25 }, {   1, 246, 25 },
    {   1, 242, 25 }, {   1, 244, 25 }, {   1, 230, 25 }, {   1, 119, 25 },
    {   1, 193, 25 }, {   1, 194, 25 }, {   1, 186, 25 }, {   1, 187, 25 },
    {   1, 211, 25 }, {   1, 190, 25 }, {   1, 163, 25 }, {   1, 164, 25 },
    {   1, 220, 25 }, {   1, 221, 25 }, {   1, 216, 25 }, {   1, 217, 25 },
    {   1, 212, 25 }, {   1, 213, 25 }, {   1, 206, 25 }, {   1, 208, 25 },
    {   1, 209, 25 }, {   1, 210, 25 }, {   1, 122, 25 }, {   1, 248, 25 },
    {   1, 235, 25 }, {   1, 148, 25 }, {   1, 253, 25 }, {   1, 254, 25 },
    {   1, 123, 25 }, {   1, 124, 25 }, {   1, 215, 25 }, {   1, 117, 25 },
    {   1, 234, 25 }, {   1, 236, 25 }, {   1, 142, 25 }, {   1, 143, 25 },
    {   1, 153, 25 }, {   1, 154, 25 }, {   1, 176, 25 }, {   1, 177, 25 },
    {   1, 174, 25 }, {   1, 175, 25 }, {   1, 184, 25 }, {   1, 185, 25 },
    {   1, 240, 25 }, {   1, 241, 25 }, {   1, 239, 25 }, {   1, 141, 25 },
    {   1, 139, 25 }, {   1, 140, 25 }, {   1, 125, 25 }, {   1, 126, 25 },
    {   1, 130, 25 }, {   1, 131, 25 }, {   1, 228, 25 }, {   1, 229, 25 },
    {   1, 232, 25 }, {   1, 233, 25 }, {   1, 132, 25 }, {   1, 133, 25 },
    {   1, 137, 25 }, {   1, 138, 25 }, {   1, 255, 25 }, {   1, 116, 26 },
    {   0, 255, 26 }, {   1, 249, 25 }, {   1, 250, 25 }, {   1, 251, 25 },
    {   1, 252, 25 }, {   1, 120, 25 }, {   1, 121, 25 }, {   1, 222, 25 },
    {   1, 224, 25 }, {   1, 218, 25 }, {   1, 219, 25 }, {   1, 200, 25 },
    {   1, 201, 25 }, {   1, 191, 25 }, {   1, 192, 25 }, {   1, 149, 25 },
    {   1, 150, 25 }, {   1, 151, 25 }, {   1, 152, 25 }, {   1, 146, 25 },
    {   1, 147, 25 }, {   1, 144, 25 }, {   1, 145, 25 }, {   1, 165, 25 },
    {   1, 166, 25 }, {   1, 167, 25 }, {   1, 168, 25 }, {   1, 158, 25 },
    {   1, 159, 25 }, {   1, 223, 25 }, {   1, 169, 25 }, {   1, 207, 25 },
    {   1, 197, 25 }, {   1, 202, 25 }, {   1, 203, 25 }, {   1, 188, 25 },
    {   1, 189, 25 }, {   1, 225, 25 }, {   1, 226, 25 }, {   1, 195, 25 },
    {   1, 196, 25 }, {   1, 182, 25 }, {   1, 183, 25 }, {   1, 231, 25 },
    {   1, 155, 25 }, {   1, 160, 25 }, {   1, 161, 25 }, {   1,  48, 16 },
    {   1,  36, 14 }, {   1,  30, 13 }, {   1,  19, 11 }, {   1,  11,  9 },
    {  32,   0,  9 }, {   1,  15, 10 }, {   1,  24, 12 }, {   1,  42, 15 },
    {   1,  62, 18 }, {   1,  70, 19 }, {   1,  94, 22 }, {   1, 180, 25 },
    {   1, 181, 25 }, {   1, 109, 24 }, {   1, 102, 23 }, {   1,  86, 21 },
    {   1,  78, 20 }, {   1,  55, 17 }, {   1,  49, 16 }, {   1,  37, 14 },
    {   1,  31, 13 }, {   1,  25, 12 }, {   1,  43, 15 }, {   1,  63, 18 },
    {   1,  71, 19 }, {   1,  79, 20 }, {   1,  87, 21 }, {   1,  88, 21 },
    {   1,  56, 17 }, {   1,  50, 16 }, {   1,  38, 14 }, { 180,   0, 13 },
    {   1,   6,  7 }, {   1,   3,  5 }, {   1,  12,  9 }, {   1,  20, 11 },
    { 100,   0, 11 }, {  60,   0, 10 }, {  20,   0,  8 }, {  12,   0,  7 },
    {   1,   9,  8 }, {   1,  16, 10 }, {   1,  32, 13 }, {   1,  44, 15 },
    {   1,  64, 18 }, {   1,  72, 19 }, {   1,  80, 20 }, {   1,  95, 22 },
    {   1,  96, 22 }, {   1, 110, 24 }, {   1, 111, 24 }, {   1, 103, 23 },
    {   1, 104, 23 }, {   1, 105, 23 }, {   1,  57, 17 }, {   1,  65, 18 },
    {   1,  73, 19 }, {   1,  81, 20 }, {   1,  89, 21 }, {   1,  97, 22 },
    {   1,  98, 22 }, {   1,  58, 17 }, {   1,  39, 14 }, {   1,  26, 12 },
    {   1,  21, 11 }, {   1,  13,  9 }, {   1,   7,  7 }, {   1,   5,  6 },
    {   1,  10,  8 }, {   1,  33, 13 }, {   1,  51, 16 }, {   1,  66, 18 },
    {   1,  74, 19 }, {   1,  82, 20 }, {   1,  90, 21 }, {   1,  91, 21 },
    {   1,  59, 17 }, {   1,  45, 15 }, {   1,  52, 16 }, {   1,  67, 18 },
    {   1,  75, 19 }, {   1,  83, 20 }, {   1, 112, 24 }, {   1, 113, 24 },
    {   1, 106, 23 }, {   1,  99, 22 }, {   1,  92, 21 }, {   1,  68, 18 },
    {   1,  76, 19 }, {   1,  84, 20 }, {   1, 107, 23 }, {   1, 114, 24 },
    {   1, 115, 24 }, {   1, 100, 22 }, {   1,  93, 21 }, {   1,  46, 15 },
    {   1,  27, 12 }, {   1,  34, 13 }, { 320,   0, 13 }, {   1,  28, 12 },
    {   1,  17, 10 }, {   1,  22, 11 }, {   1,  40, 14 }, {   1,  53, 16 },
    {   1,  60, 17 }, {   1,  61, 17 }, {   1,  47, 15 }, {   1,  35, 13 },
    {   1,  29, 12 }, {   1,  18, 10 }, {   1,   8,  7 }, {   1,   2,  3 },
};

static av_cold int cfhd_init_vlc(CFHD_RL_VLC_ELEM out[], unsigned out_size,
                                 const CFHD_RL_ELEM table_vlc[], unsigned table_size,
                                 CFHD_RL_VLC_ELEM tmp[], void *logctx)
{
    VLC vlc;
    unsigned j;
    int ret;

    /** Similar to dv.c, generate signed VLC tables **/

    for (unsigned i = j = 0; i < table_size; i++, j++) {
        tmp[j].len   = table_vlc[i].len;
        tmp[j].run   = table_vlc[i].run;
        tmp[j].level = table_vlc[i].level;

        /* Don't include the zero level nor escape bits */
        if (table_vlc[i].level && table_vlc[i].run) {
            tmp[j].len++;
            j++;
            tmp[j].len   =  table_vlc[i].len + 1;
            tmp[j].run   =  table_vlc[i].run;
            tmp[j].level = -table_vlc[i].level;
        }
    }

    ret = ff_vlc_init_from_lengths(&vlc, VLC_BITS, j,
                                   &tmp[0].len, sizeof(tmp[0]),
                                   NULL, 0, 0, 0, 0, logctx);
    if (ret < 0)
        return ret;
    av_assert0(vlc.table_size == out_size);

    for (unsigned i = out_size; i-- > 0;) {
        int code = vlc.table[i].sym;
        int len  = vlc.table[i].len;
        int level, run;

        if (len < 0) { // more bits needed
            run   = 0;
            level = code;
        } else {
            run   = tmp[code].run;
            level = tmp[code].level;
        }
        out[i].len   = len;
        out[i].level = level;
        out[i].run   = run;
    }
    ff_vlc_free(&vlc);

    return 0;
}

av_cold int ff_cfhd_init_vlcs(CFHDContext *s)
{
    int ret;

    /* Table 18 - we reuse the unused table_9_rl_vlc as scratch buffer here */
    ret = cfhd_init_vlc(s->table_18_rl_vlc, FF_ARRAY_ELEMS(s->table_18_rl_vlc),
                        table_18_vlc,       FF_ARRAY_ELEMS(table_18_vlc),
                        s->table_9_rl_vlc, s->avctx);
    if (ret < 0)
        return ret;
    /* Table 9 - table_9_rl_vlc itself is used as scratch buffer; it works
     * because we are counting down in the final loop */
    ret = cfhd_init_vlc(s->table_9_rl_vlc, FF_ARRAY_ELEMS(s->table_9_rl_vlc),
                        table_9_vlc,       FF_ARRAY_ELEMS(table_9_vlc),
                        s->table_9_rl_vlc, s->avctx);
    if (ret < 0)
        return ret;
    return 0;
}
