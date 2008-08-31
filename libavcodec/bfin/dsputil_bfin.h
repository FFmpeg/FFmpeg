/*
 * BlackFin DSPUTILS COMMON OPTIMIZATIONS HEADER
 *
 * Copyright (C) 2007 Marc Hoffman <mmh@pleasantst.com>
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


#ifndef AVCODEC_BFIN_DSPUTIL_BFIN_H
#define AVCODEC_BFIN_DSPUTIL_BFIN_H

#ifdef __FDPIC__
#define attribute_l1_text  __attribute__ ((l1_text))
#define attribute_l1_data_b __attribute__((l1_data_B))
#else
#define attribute_l1_text
#define attribute_l1_data_b
#endif

#ifdef BFIN_PROFILE

static double Telem[16];
static char  *TelemNames[16];
static int    TelemCnt;

#define PROF(lab,e) { int xx_e = e; char*xx_lab = lab; uint64_t xx_t0 = read_time();
#define EPROF()       xx_t0 = read_time()-xx_t0; Telem[xx_e] = Telem[xx_e] + xx_t0; TelemNames[xx_e] = xx_lab; }

static void prof_report (void)
{
    int i;
    double s = 0;
    for (i=0;i<16;i++) {
        double v;
        if (TelemNames[i]) {
            v = Telem[i]/TelemCnt;
            av_log (NULL,AV_LOG_DEBUG,"%-20s: %12.4f\t%12.4f\n", TelemNames[i],v,v/64);
            s = s + Telem[i];
        }
    }
    av_log (NULL,AV_LOG_DEBUG,"%-20s: %12.4f\t%12.4f\n%20.4f\t%d\n",
            "total",s/TelemCnt,s/TelemCnt/64,s,TelemCnt);
}

static void bfprof (void)
{
    static int init;
    if (!init) atexit (prof_report);
    init=1;
    TelemCnt++;
}

#else
#define PROF(a,b)
#define EPROF()
#define bfprof()
#endif

#endif /* AVCODEC_BFIN_DSPUTIL_BFIN_H */
