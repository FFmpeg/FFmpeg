/**
 * @file libavcodec/vorbis.c
 * Common code for Vorbis I encoder and decoder
 * @author Denes Balatoni  ( dbalatoni programozo hu )

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

#undef V_DEBUG
//#define V_DEBUG

#define ALT_BITSTREAM_READER_LE
#include "avcodec.h"
#include "bitstream.h"

#include "vorbis.h"


/* Helper functions */

unsigned int ff_vorbis_nth_root(unsigned int x, unsigned int n) {   // x^(1/n)
    unsigned int ret=0, i, j;

    do {
        ++ret;
        for(i=0,j=ret;i<n-1;i++) j*=ret;
    } while (j<=x);

    return ret - 1;
}

// Generate vlc codes from vorbis huffman code lengths

// the two bits[p] > 32 checks should be redundant, all calling code should
// already ensure that, but since it allows overwriting the stack it seems
// reasonable to check redundantly.
int ff_vorbis_len2vlc(uint8_t *bits, uint32_t *codes, uint_fast32_t num) {
    uint_fast32_t exit_at_level[33]={404,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

    uint_fast8_t i,j;
    uint_fast32_t code,p;

#ifdef V_DEBUG
    GetBitContext gb;
#endif

    for(p=0;(bits[p]==0) && (p<num);++p);
    if (p==num) {
//        av_log(vc->avccontext, AV_LOG_INFO, "An empty codebook. Heh?! \n");
        return 0;
    }

    codes[p]=0;
    if (bits[p] > 32) return 1;
    for(i=0;i<bits[p];++i) {
        exit_at_level[i+1]=1<<i;
    }

#ifdef V_DEBUG
    av_log(NULL, AV_LOG_INFO, " %d. of %d code len %d code %d - ", p, num, bits[p], codes[p]);
    init_get_bits(&gb, (uint_fast8_t *)&codes[p], bits[p]);
    for(i=0;i<bits[p];++i) {
        av_log(NULL, AV_LOG_INFO, "%s", get_bits1(&gb) ? "1" : "0");
    }
    av_log(NULL, AV_LOG_INFO, "\n");
#endif

    ++p;

    for(;p<num;++p) {
        if (bits[p] > 32) return 1;
        if (bits[p]==0) continue;
        // find corresponding exit(node which the tree can grow further from)
        for(i=bits[p];i>0;--i) {
            if (exit_at_level[i]) break;
        }
        if (!i) return 1; // overspecified tree
        code=exit_at_level[i];
        exit_at_level[i]=0;
        // construct code (append 0s to end) and introduce new exits
        for(j=i+1;j<=bits[p];++j) {
            exit_at_level[j]=code+(1<<(j-1));
        }
        codes[p]=code;

#ifdef V_DEBUG
        av_log(NULL, AV_LOG_INFO, " %d. code len %d code %d - ", p, bits[p], codes[p]);
        init_get_bits(&gb, (uint_fast8_t *)&codes[p], bits[p]);
        for(i=0;i<bits[p];++i) {
            av_log(NULL, AV_LOG_INFO, "%s", get_bits1(&gb) ? "1" : "0");
        }
        av_log(NULL, AV_LOG_INFO, "\n");
#endif

    }

    //no exits should be left (underspecified tree - ie. unused valid vlcs - not allowed by SPEC)
    for (p=1; p<33; p++)
        if (exit_at_level[p]) return 1;

    return 0;
}

void ff_vorbis_ready_floor1_list(vorbis_floor1_entry * list, int values) {
    int i;
    list[0].sort = 0;
    list[1].sort = 1;
    for (i = 2; i < values; i++) {
        int j;
        list[i].low = 0;
        list[i].high = 1;
        list[i].sort = i;
        for (j = 2; j < i; j++) {
            int tmp = list[j].x;
            if (tmp < list[i].x) {
                if (tmp > list[list[i].low].x) list[i].low = j;
            } else {
                if (tmp < list[list[i].high].x) list[i].high = j;
            }
        }
    }
    for (i = 0; i < values - 1; i++) {
        int j;
        for (j = i + 1; j < values; j++) {
            if (list[list[i].sort].x > list[list[j].sort].x) {
                int tmp = list[i].sort;
                list[i].sort = list[j].sort;
                list[j].sort = tmp;
            }
        }
    }
}

static void render_line(int x0, int y0, int x1, int y1, float * buf) {
    int dy = y1 - y0;
    int adx = x1 - x0;
    int base = dy / adx;
    int ady = FFABS(dy) - FFABS(base) * adx;
    int x = x0;
    int y = y0;
    int err = 0;
    int sy = dy<0 ? -1 : 1;
    buf[x] = ff_vorbis_floor1_inverse_db_table[y];
    while (++x < x1) {
        err += ady;
        if (err >= adx) {
            err -= adx;
            y += sy;
        }
        y += base;
        buf[x] = ff_vorbis_floor1_inverse_db_table[y];
    }
}

void ff_vorbis_floor1_render_list(vorbis_floor1_entry * list, int values, uint_fast16_t * y_list, int * flag, int multiplier, float * out, int samples) {
    int lx, ly, i;
    lx = 0;
    ly = y_list[0] * multiplier;
    for (i = 1; i < values; i++) {
        int pos = list[i].sort;
        if (flag[pos]) {
            int x1 = list[pos].x;
            int y1 = y_list[pos] * multiplier;
            if (lx < samples)
                render_line(lx, ly, FFMIN(x1,samples), y1, out);
            lx = x1;
            ly = y1;
        }
        if (lx >= samples) break;
    }
    if (lx < samples) render_line(lx, ly, samples, ly, out);
}
