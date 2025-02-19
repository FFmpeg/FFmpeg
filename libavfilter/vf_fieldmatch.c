/*
 * Copyright (c) 2012 Fredrik Mellbin
 * Copyright (c) 2013 Clément Bœsch
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
 * Fieldmatching filter, ported from VFM filter (VapourSynth) by Clément.
 * Fredrik Mellbin is the author of the VIVTC/VFM filter, which is itself a
 * light clone of the TIVTC/TFM (AviSynth) filter written by Kevin Stone
 * (tritical), the original author.
 *
 * @see http://bengal.missouri.edu/~kes25c/
 * @see http://www.vapoursynth.com/about/
 */

#include <inttypes.h>

#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/timestamp.h"
#include "avfilter.h"
#include "filters.h"
#include "formats.h"
#include "video.h"

#define INPUT_MAIN     0
#define INPUT_CLEANSRC 1

enum fieldmatch_parity {
    FM_PARITY_AUTO   = -1,
    FM_PARITY_BOTTOM =  0,
    FM_PARITY_TOP    =  1,
};

enum matching_mode {
    MODE_PC,
    MODE_PC_N,
    MODE_PC_U,
    MODE_PC_N_UB,
    MODE_PCN,
    MODE_PCN_UB,
    NB_MODE
};

enum comb_matching_mode {
    COMBMATCH_NONE,
    COMBMATCH_SC,
    COMBMATCH_FULL,
    NB_COMBMATCH
};

enum comb_dbg {
    COMBDBG_NONE,
    COMBDBG_PCN,
    COMBDBG_PCNUB,
    NB_COMBDBG
};

typedef struct FieldMatchContext {
    const AVClass *class;

    AVFrame *prv,  *src,  *nxt;     ///< main sliding window of 3 frames
    AVFrame *prv2, *src2, *nxt2;    ///< sliding window of the optional second stream
    int got_frame[2];               ///< frame request flag for each input stream
    int hsub[2], vsub[2];           ///< chroma subsampling values
    int bpc;                        ///< bytes per component
    uint32_t eof;                   ///< bitmask for end of stream
    int64_t lastscdiff;
    int64_t lastn;

    /* options */
    int order;
    int ppsrc;
    int mode;                       ///< matching_mode
    int field;
    int mchroma;
    int y0, y1;
    int64_t scthresh;
    double scthresh_flt;
    int combmatch;                  ///< comb_matching_mode
    int combdbg;
    int cthresh;
    int chroma;
    int blockx, blocky;
    int combpel;

    /* misc buffers */
    uint8_t *map_data[4];
    int map_linesize[4];
    uint8_t *cmask_data[4];
    int cmask_linesize[4];
    int *c_array;
    int tpitchy, tpitchuv;
    uint8_t *tbuffer;
} FieldMatchContext;

#define OFFSET(x) offsetof(FieldMatchContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption fieldmatch_options[] = {
    { "order", "specify the assumed field order", OFFSET(order), AV_OPT_TYPE_INT, {.i64=FM_PARITY_AUTO}, -1, 1, FLAGS, .unit = "order" },
        { "auto", "auto detect parity",        0, AV_OPT_TYPE_CONST, {.i64=FM_PARITY_AUTO},    INT_MIN, INT_MAX, FLAGS, .unit = "order" },
        { "bff",  "assume bottom field first", 0, AV_OPT_TYPE_CONST, {.i64=FM_PARITY_BOTTOM},  INT_MIN, INT_MAX, FLAGS, .unit = "order" },
        { "tff",  "assume top field first",    0, AV_OPT_TYPE_CONST, {.i64=FM_PARITY_TOP},     INT_MIN, INT_MAX, FLAGS, .unit = "order" },
    { "mode", "set the matching mode or strategy to use", OFFSET(mode), AV_OPT_TYPE_INT, {.i64=MODE_PC_N}, MODE_PC, NB_MODE-1, FLAGS, .unit = "mode" },
        { "pc",      "2-way match (p/c)",                                                                    0, AV_OPT_TYPE_CONST, {.i64=MODE_PC},      INT_MIN, INT_MAX, FLAGS, .unit = "mode" },
        { "pc_n",    "2-way match + 3rd match on combed (p/c + u)",                                          0, AV_OPT_TYPE_CONST, {.i64=MODE_PC_N},    INT_MIN, INT_MAX, FLAGS, .unit = "mode" },
        { "pc_u",    "2-way match + 3rd match (same order) on combed (p/c + u)",                             0, AV_OPT_TYPE_CONST, {.i64=MODE_PC_U},    INT_MIN, INT_MAX, FLAGS, .unit = "mode" },
        { "pc_n_ub", "2-way match + 3rd match on combed + 4th/5th matches if still combed (p/c + u + u/b)",  0, AV_OPT_TYPE_CONST, {.i64=MODE_PC_N_UB}, INT_MIN, INT_MAX, FLAGS, .unit = "mode" },
        { "pcn",     "3-way match (p/c/n)",                                                                  0, AV_OPT_TYPE_CONST, {.i64=MODE_PCN},     INT_MIN, INT_MAX, FLAGS, .unit = "mode" },
        { "pcn_ub",  "3-way match + 4th/5th matches on combed (p/c/n + u/b)",                                0, AV_OPT_TYPE_CONST, {.i64=MODE_PCN_UB},  INT_MIN, INT_MAX, FLAGS, .unit = "mode" },
    { "ppsrc", "mark main input as a pre-processed input and activate clean source input stream", OFFSET(ppsrc), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS },
    { "field", "set the field to match from", OFFSET(field), AV_OPT_TYPE_INT, {.i64=FM_PARITY_AUTO}, -1, 1, FLAGS, .unit = "field" },
        { "auto",   "automatic (same value as 'order')",    0, AV_OPT_TYPE_CONST, {.i64=FM_PARITY_AUTO},    INT_MIN, INT_MAX, FLAGS, .unit = "field" },
        { "bottom", "bottom field",                         0, AV_OPT_TYPE_CONST, {.i64=FM_PARITY_BOTTOM},  INT_MIN, INT_MAX, FLAGS, .unit = "field" },
        { "top",    "top field",                            0, AV_OPT_TYPE_CONST, {.i64=FM_PARITY_TOP},     INT_MIN, INT_MAX, FLAGS, .unit = "field" },
    { "mchroma", "set whether or not chroma is included during the match comparisons", OFFSET(mchroma), AV_OPT_TYPE_BOOL, {.i64=1}, 0, 1,  FLAGS },
    { "y0", "define an exclusion band which excludes the lines between y0 and y1 from the field matching decision", OFFSET(y0), AV_OPT_TYPE_INT, {.i64=0}, 0, INT_MAX, FLAGS },
    { "y1", "define an exclusion band which excludes the lines between y0 and y1 from the field matching decision", OFFSET(y1), AV_OPT_TYPE_INT, {.i64=0}, 0, INT_MAX, FLAGS },
    { "scthresh", "set scene change detection threshold", OFFSET(scthresh_flt), AV_OPT_TYPE_DOUBLE, {.dbl=12}, 0, 100, FLAGS },
    { "combmatch", "set combmatching mode", OFFSET(combmatch), AV_OPT_TYPE_INT, {.i64=COMBMATCH_SC}, COMBMATCH_NONE, NB_COMBMATCH-1, FLAGS, .unit = "combmatching" },
        { "none", "disable combmatching",                     0, AV_OPT_TYPE_CONST, {.i64=COMBMATCH_NONE}, INT_MIN, INT_MAX, FLAGS, .unit = "combmatching" },
        { "sc",   "enable combmatching only on scene change", 0, AV_OPT_TYPE_CONST, {.i64=COMBMATCH_SC},   INT_MIN, INT_MAX, FLAGS, .unit = "combmatching" },
        { "full", "enable combmatching all the time",         0, AV_OPT_TYPE_CONST, {.i64=COMBMATCH_FULL}, INT_MIN, INT_MAX, FLAGS, .unit = "combmatching" },
    { "combdbg",   "enable comb debug", OFFSET(combdbg), AV_OPT_TYPE_INT, {.i64=COMBDBG_NONE}, COMBDBG_NONE, NB_COMBDBG-1, FLAGS, .unit = "dbglvl" },
        { "none",  "no forced calculation", 0, AV_OPT_TYPE_CONST, {.i64=COMBDBG_NONE},  INT_MIN, INT_MAX, FLAGS, .unit = "dbglvl" },
        { "pcn",   "calculate p/c/n",       0, AV_OPT_TYPE_CONST, {.i64=COMBDBG_PCN},   INT_MIN, INT_MAX, FLAGS, .unit = "dbglvl" },
        { "pcnub", "calculate p/c/n/u/b",   0, AV_OPT_TYPE_CONST, {.i64=COMBDBG_PCNUB}, INT_MIN, INT_MAX, FLAGS, .unit = "dbglvl" },
    { "cthresh", "set the area combing threshold used for combed frame detection",       OFFSET(cthresh), AV_OPT_TYPE_INT, {.i64= 9}, -1, 0xff, FLAGS },
    { "chroma",  "set whether or not chroma is considered in the combed frame decision", OFFSET(chroma),  AV_OPT_TYPE_BOOL,{.i64= 0},  0,    1, FLAGS },
    { "blockx",  "set the x-axis size of the window used during combed frame detection", OFFSET(blockx),  AV_OPT_TYPE_INT, {.i64=16},  4, 1<<9, FLAGS },
    { "blocky",  "set the y-axis size of the window used during combed frame detection", OFFSET(blocky),  AV_OPT_TYPE_INT, {.i64=16},  4, 1<<9, FLAGS },
    { "combpel", "set the number of combed pixels inside any of the blocky by blockx size blocks on the frame for the frame to be detected as combed", OFFSET(combpel), AV_OPT_TYPE_INT, {.i64=80}, 0, INT_MAX, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(fieldmatch);

static int get_width(const FieldMatchContext *fm, const AVFrame *f, int plane, int input)
{
    return plane ? AV_CEIL_RSHIFT(f->width, fm->hsub[input]) : f->width;
}

static int get_height(const FieldMatchContext *fm, const AVFrame *f, int plane, int input)
{
    return plane ? AV_CEIL_RSHIFT(f->height, fm->vsub[input]) : f->height;
}

static int64_t luma_abs_diff(const AVFrame *f1, const AVFrame *f2)
{
    int x, y;
    const uint8_t *srcp1 = f1->data[0];
    const uint8_t *srcp2 = f2->data[0];
    const int src1_linesize = f1->linesize[0];
    const int src2_linesize = f2->linesize[0];
    const int width  = f1->width;
    const int height = f1->height;
    int64_t acc = 0;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++)
            acc += abs(srcp1[x] - srcp2[x]);
        srcp1 += src1_linesize;
        srcp2 += src2_linesize;
    }
    return acc;
}

static void fill_buf(uint8_t *data, int w, int h, int linesize, uint8_t v)
{
    int y;

    for (y = 0; y < h; y++) {
        memset(data, v, w);
        data += linesize;
    }
}

static int calc_combed_score(const FieldMatchContext *fm, const AVFrame *src)
{
    int x, y, plane, max_v = 0;
    const int cthresh = fm->cthresh;
    const int cthresh6 = cthresh * 6;

    for (plane = 0; plane < (fm->chroma ? 3 : 1); plane++) {
        const uint8_t *srcp = src->data[plane];
        const int src_linesize = src->linesize[plane];
        const int width  = get_width (fm, src, plane, INPUT_MAIN);
        const int height = get_height(fm, src, plane, INPUT_MAIN);
        uint8_t *cmkp = fm->cmask_data[plane];
        const int cmk_linesize = fm->cmask_linesize[plane];

        if (cthresh < 0) {
            fill_buf(cmkp, width, height, cmk_linesize, 0xff);
            continue;
        }
        fill_buf(cmkp, width, height, cmk_linesize, 0);

        /* [1 -3 4 -3 1] vertical filter */
#define FILTER(xm2, xm1, xp1, xp2) \
        abs(  4 * srcp[x] \
             -3 * (srcp[x + (xm1)*src_linesize] + srcp[x + (xp1)*src_linesize]) \
             +    (srcp[x + (xm2)*src_linesize] + srcp[x + (xp2)*src_linesize])) > cthresh6

        /* first line */
        for (x = 0; x < width; x++) {
            const int s1 = abs(srcp[x] - srcp[x + src_linesize]);
            if (s1 > cthresh && FILTER(2, 1, 1, 2))
                cmkp[x] = 0xff;
        }
        srcp += src_linesize;
        cmkp += cmk_linesize;

        /* second line */
        for (x = 0; x < width; x++) {
            const int s1 = abs(srcp[x] - srcp[x - src_linesize]);
            const int s2 = abs(srcp[x] - srcp[x + src_linesize]);
            if (s1 > cthresh && s2 > cthresh && FILTER(2, -1, 1, 2))
                cmkp[x] = 0xff;
        }
        srcp += src_linesize;
        cmkp += cmk_linesize;

        /* all lines minus first two and last two */
        for (y = 2; y < height-2; y++) {
            for (x = 0; x < width; x++) {
                const int s1 = abs(srcp[x] - srcp[x - src_linesize]);
                const int s2 = abs(srcp[x] - srcp[x + src_linesize]);
                if (s1 > cthresh && s2 > cthresh && FILTER(-2, -1, 1, 2))
                    cmkp[x] = 0xff;
            }
            srcp += src_linesize;
            cmkp += cmk_linesize;
        }

        /* before-last line */
        for (x = 0; x < width; x++) {
            const int s1 = abs(srcp[x] - srcp[x - src_linesize]);
            const int s2 = abs(srcp[x] - srcp[x + src_linesize]);
            if (s1 > cthresh && s2 > cthresh && FILTER(-2, -1, 1, -2))
                cmkp[x] = 0xff;
        }
        srcp += src_linesize;
        cmkp += cmk_linesize;

        /* last line */
        for (x = 0; x < width; x++) {
            const int s1 = abs(srcp[x] - srcp[x - src_linesize]);
            if (s1 > cthresh && FILTER(-2, -1, -1, -2))
                cmkp[x] = 0xff;
        }
    }

    if (fm->chroma) {
        uint8_t *cmkp  = fm->cmask_data[0];
        uint8_t *cmkpU = fm->cmask_data[1];
        uint8_t *cmkpV = fm->cmask_data[2];
        const int width  = AV_CEIL_RSHIFT(src->width,  fm->hsub[INPUT_MAIN]);
        const int height = AV_CEIL_RSHIFT(src->height, fm->vsub[INPUT_MAIN]);
        const int cmk_linesize   = fm->cmask_linesize[0] << 1;
        const int cmk_linesizeUV = fm->cmask_linesize[2];
        uint8_t *cmkpp  = cmkp - (cmk_linesize>>1);
        uint8_t *cmkpn  = cmkp + (cmk_linesize>>1);
        uint8_t *cmkpnn = cmkp +  cmk_linesize;
        for (y = 1; y < height - 1; y++) {
            cmkpp  += cmk_linesize;
            cmkp   += cmk_linesize;
            cmkpn  += cmk_linesize;
            cmkpnn += cmk_linesize;
            cmkpV  += cmk_linesizeUV;
            cmkpU  += cmk_linesizeUV;
            for (x = 1; x < width - 1; x++) {
#define HAS_FF_AROUND(p, lz) (p[(x)-1 - (lz)] == 0xff || p[(x) - (lz)] == 0xff || p[(x)+1 - (lz)] == 0xff || \
                              p[(x)-1       ] == 0xff ||                          p[(x)+1       ] == 0xff || \
                              p[(x)-1 + (lz)] == 0xff || p[(x) + (lz)] == 0xff || p[(x)+1 + (lz)] == 0xff)
                if ((cmkpV[x] == 0xff && HAS_FF_AROUND(cmkpV, cmk_linesizeUV)) ||
                    (cmkpU[x] == 0xff && HAS_FF_AROUND(cmkpU, cmk_linesizeUV))) {
                    ((uint16_t*)cmkp)[x]  = 0xffff;
                    ((uint16_t*)cmkpn)[x] = 0xffff;
                    if (y&1) ((uint16_t*)cmkpp)[x]  = 0xffff;
                    else     ((uint16_t*)cmkpnn)[x] = 0xffff;
                }
            }
        }
    }

    {
        const int blockx = fm->blockx;
        const int blocky = fm->blocky;
        const int xhalf = blockx/2;
        const int yhalf = blocky/2;
        const int cmk_linesize = fm->cmask_linesize[0];
        const uint8_t *cmkp    = fm->cmask_data[0] + cmk_linesize;
        const int width  = src->width;
        const int height = src->height;
        const int xblocks = ((width+xhalf)/blockx) + 1;
        const int xblocks4 = xblocks<<2;
        const int yblocks = ((height+yhalf)/blocky) + 1;
        int *c_array = fm->c_array;
        const int arraysize = (xblocks*yblocks)<<2;
        int      heighta = (height/(blocky/2))*(blocky/2);
        const int widtha = (width /(blockx/2))*(blockx/2);
        if (heighta == height)
            heighta = height - yhalf;
        memset(c_array, 0, arraysize * sizeof(*c_array));

#define C_ARRAY_ADD(v) do {                         \
    const int box1 = (x / blockx) * 4;              \
    const int box2 = ((x + xhalf) / blockx) * 4;    \
    c_array[temp1 + box1    ] += v;                 \
    c_array[temp1 + box2 + 1] += v;                 \
    c_array[temp2 + box1 + 2] += v;                 \
    c_array[temp2 + box2 + 3] += v;                 \
} while (0)

#define VERTICAL_HALF(y_start, y_end) do {                                  \
    for (y = y_start; y < y_end; y++) {                                     \
        const int temp1 = (y / blocky) * xblocks4;                          \
        const int temp2 = ((y + yhalf) / blocky) * xblocks4;                \
        for (x = 0; x < width; x++)                                         \
            if (cmkp[x - cmk_linesize] == 0xff &&                           \
                cmkp[x               ] == 0xff &&                           \
                cmkp[x + cmk_linesize] == 0xff)                             \
                C_ARRAY_ADD(1);                                             \
        cmkp += cmk_linesize;                                               \
    }                                                                       \
} while (0)

        VERTICAL_HALF(1, yhalf);

        for (y = yhalf; y < heighta; y += yhalf) {
            const int temp1 = (y / blocky) * xblocks4;
            const int temp2 = ((y + yhalf) / blocky) * xblocks4;

            for (x = 0; x < widtha; x += xhalf) {
                const uint8_t *cmkp_tmp = cmkp + x;
                int u, v, sum = 0;
                for (u = 0; u < yhalf; u++) {
                    for (v = 0; v < xhalf; v++)
                        if (cmkp_tmp[v - cmk_linesize] == 0xff &&
                            cmkp_tmp[v               ] == 0xff &&
                            cmkp_tmp[v + cmk_linesize] == 0xff)
                            sum++;
                    cmkp_tmp += cmk_linesize;
                }
                if (sum)
                    C_ARRAY_ADD(sum);
            }

            for (x = widtha; x < width; x++) {
                const uint8_t *cmkp_tmp = cmkp + x;
                int u, sum = 0;
                for (u = 0; u < yhalf; u++) {
                    if (cmkp_tmp[-cmk_linesize] == 0xff &&
                        cmkp_tmp[            0] == 0xff &&
                        cmkp_tmp[ cmk_linesize] == 0xff)
                        sum++;
                    cmkp_tmp += cmk_linesize;
                }
                if (sum)
                    C_ARRAY_ADD(sum);
            }

            cmkp += cmk_linesize * yhalf;
        }

        VERTICAL_HALF(heighta, height - 1);

        for (x = 0; x < arraysize; x++)
            if (c_array[x] > max_v)
                max_v = c_array[x];
    }
    return max_v;
}

// the secret is that tbuffer is an interlaced, offset subset of all the lines
static void build_abs_diff_mask(const uint8_t *prvp, int prv_linesize,
                                const uint8_t *nxtp, int nxt_linesize,
                                uint8_t *tbuffer,    int tbuf_linesize,
                                int width, int height)
{
    int y, x;

    prvp -= prv_linesize;
    nxtp -= nxt_linesize;
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++)
            tbuffer[x] = FFABS(prvp[x] - nxtp[x]);
        prvp += prv_linesize;
        nxtp += nxt_linesize;
        tbuffer += tbuf_linesize;
    }
}

/**
 * Build a map over which pixels differ a lot/a little
 */
static void build_diff_map(FieldMatchContext *fm,
                           const uint8_t *prvp, int prv_linesize,
                           const uint8_t *nxtp, int nxt_linesize,
                           uint8_t *dstp, int dst_linesize, int height,
                           int width, int plane)
{
    int x, y, u, diff, count;
    int tpitch = plane ? fm->tpitchuv : fm->tpitchy;
    const uint8_t *dp = fm->tbuffer + tpitch;

    build_abs_diff_mask(prvp, prv_linesize, nxtp, nxt_linesize,
                        fm->tbuffer, tpitch, width, height>>1);

    for (y = 2; y < height - 2; y += 2) {
        for (x = 1; x < width - 1; x++) {
            diff = dp[x];
            if (diff > 3) {
                for (count = 0, u = x-1; u < x+2 && count < 2; u++) {
                    count += dp[u-tpitch] > 3;
                    count += dp[u       ] > 3;
                    count += dp[u+tpitch] > 3;
                }
                if (count > 1) {
                    dstp[x] = 1;
                    if (diff > 19) {
                        int upper = 0, lower = 0;
                        for (count = 0, u = x-1; u < x+2 && count < 6; u++) {
                            if (dp[u-tpitch] > 19) { count++; upper = 1; }
                            if (dp[u       ] > 19)   count++;
                            if (dp[u+tpitch] > 19) { count++; lower = 1; }
                        }
                        if (count > 3) {
                            if (upper && lower) {
                                dstp[x] |= 1<<1;
                            } else {
                                int upper2 = 0, lower2 = 0;
                                for (u = FFMAX(x-4,0); u < FFMIN(x+5,width); u++) {
                                    if (y != 2 &&        dp[u-2*tpitch] > 19) upper2 = 1;
                                    if (                 dp[u-  tpitch] > 19) upper  = 1;
                                    if (                 dp[u+  tpitch] > 19) lower  = 1;
                                    if (y != height-4 && dp[u+2*tpitch] > 19) lower2 = 1;
                                }
                                if ((upper && (lower || upper2)) ||
                                    (lower && (upper || lower2)))
                                    dstp[x] |= 1<<1;
                                else if (count > 5)
                                    dstp[x] |= 1<<2;
                            }
                        }
                    }
                }
            }
        }
        dp += tpitch;
        dstp += dst_linesize;
    }
}

enum { mP, mC, mN, mB, mU };

static int get_field_base(int match, int field)
{
    return match < 3 ? 2 - field : 1 + field;
}

static AVFrame *select_frame(FieldMatchContext *fm, int match)
{
    if      (match == mP || match == mB) return fm->prv;
    else if (match == mN || match == mU) return fm->nxt;
    else  /* match == mC */              return fm->src;
}

static int compare_fields(FieldMatchContext *fm, int match1, int match2, int field)
{
    int plane, ret;
    uint64_t accumPc = 0, accumPm = 0, accumPml = 0;
    uint64_t accumNc = 0, accumNm = 0, accumNml = 0;
    int norm1, norm2, mtn1, mtn2;
    float c1, c2, mr;
    const AVFrame *src = fm->src;

    for (plane = 0; plane < (fm->mchroma ? 3 : 1); plane++) {
        int x, y, temp1, temp2, fbase;
        const AVFrame *prev, *next;
        uint8_t *mapp    = fm->map_data[plane];
        int map_linesize = fm->map_linesize[plane];
        const uint8_t *srcp = src->data[plane];
        const int src_linesize  = src->linesize[plane];
        const int srcf_linesize = src_linesize << 1;
        int prv_linesize,  nxt_linesize;
        int prvf_linesize, nxtf_linesize;
        const int width  = get_width (fm, src, plane, INPUT_MAIN);
        const int height = get_height(fm, src, plane, INPUT_MAIN);
        const int y0a = fm->y0 >> (plane ? fm->vsub[INPUT_MAIN] : 0);
        const int y1a = fm->y1 >> (plane ? fm->vsub[INPUT_MAIN] : 0);
        const int startx = (plane == 0 ? 8 : 8 >> fm->hsub[INPUT_MAIN]);
        const int stopx  = width - startx;
        const uint8_t *srcpf, *srcf, *srcnf;
        const uint8_t *prvpf, *prvnf, *nxtpf, *nxtnf;

        fill_buf(mapp, width, height, map_linesize, 0);

        /* match1 */
        fbase = get_field_base(match1, field);
        srcf  = srcp + (fbase + 1) * src_linesize;
        srcpf = srcf - srcf_linesize;
        srcnf = srcf + srcf_linesize;
        mapp  = mapp + fbase * map_linesize;
        prev = select_frame(fm, match1);
        prv_linesize  = prev->linesize[plane];
        prvf_linesize = prv_linesize << 1;
        prvpf = prev->data[plane] + fbase * prv_linesize;   // previous frame, previous field
        prvnf = prvpf + prvf_linesize;                      // previous frame, next     field

        /* match2 */
        fbase = get_field_base(match2, field);
        next = select_frame(fm, match2);
        nxt_linesize  = next->linesize[plane];
        nxtf_linesize = nxt_linesize << 1;
        nxtpf = next->data[plane] + fbase * nxt_linesize;   // next frame, previous field
        nxtnf = nxtpf + nxtf_linesize;                      // next frame, next     field

        map_linesize <<= 1;
        if ((match1 >= 3 && field == 1) || (match1 < 3 && field != 1))
            build_diff_map(fm, prvpf, prvf_linesize, nxtpf, nxtf_linesize,
                           mapp, map_linesize, height, width, plane);
        else
            build_diff_map(fm, prvnf, prvf_linesize, nxtnf, nxtf_linesize,
                           mapp + map_linesize, map_linesize, height, width, plane);

        for (y = 2; y < height - 2; y += 2) {
            if (y0a == y1a || y < y0a || y > y1a) {
                for (x = startx; x < stopx; x++) {
                    if (mapp[x] > 0 || mapp[x + map_linesize] > 0) {
                        temp1 = srcpf[x] + (srcf[x] << 2) + srcnf[x]; // [1 4 1]

                        temp2 = abs(3 * (prvpf[x] + prvnf[x]) - temp1);
                        if (temp2 > 23 && ((mapp[x]&1) || (mapp[x + map_linesize]&1)))
                            accumPc += temp2;
                        if (temp2 > 42) {
                            if ((mapp[x]&2) || (mapp[x + map_linesize]&2))
                                accumPm += temp2;
                            if ((mapp[x]&4) || (mapp[x + map_linesize]&4))
                                accumPml += temp2;
                        }

                        temp2 = abs(3 * (nxtpf[x] + nxtnf[x]) - temp1);
                        if (temp2 > 23 && ((mapp[x]&1) || (mapp[x + map_linesize]&1)))
                            accumNc += temp2;
                        if (temp2 > 42) {
                            if ((mapp[x]&2) || (mapp[x + map_linesize]&2))
                                accumNm += temp2;
                            if ((mapp[x]&4) || (mapp[x + map_linesize]&4))
                                accumNml += temp2;
                        }
                    }
                }
            }
            prvpf += prvf_linesize;
            prvnf += prvf_linesize;
            srcpf += srcf_linesize;
            srcf  += srcf_linesize;
            srcnf += srcf_linesize;
            nxtpf += nxtf_linesize;
            nxtnf += nxtf_linesize;
            mapp  += map_linesize;
        }
    }

    if (accumPm < 500 && accumNm < 500 && (accumPml >= 500 || accumNml >= 500) &&
        FFMAX(accumPml,accumNml) > 3*FFMIN(accumPml,accumNml)) {
        accumPm = accumPml;
        accumNm = accumNml;
    }

    norm1 = (int)((accumPc / 6.0f) + 0.5f);
    norm2 = (int)((accumNc / 6.0f) + 0.5f);
    mtn1  = (int)((accumPm / 6.0f) + 0.5f);
    mtn2  = (int)((accumNm / 6.0f) + 0.5f);
    c1 = ((float)FFMAX(norm1,norm2)) / ((float)FFMAX(FFMIN(norm1,norm2),1));
    c2 = ((float)FFMAX(mtn1, mtn2))  / ((float)FFMAX(FFMIN(mtn1, mtn2), 1));
    mr = ((float)FFMAX(mtn1, mtn2))  / ((float)FFMAX(FFMAX(norm1,norm2),1));
    if (((mtn1 >=  500 || mtn2 >=  500) && (mtn1*2 < mtn2*1 || mtn2*2 < mtn1*1)) ||
        ((mtn1 >= 1000 || mtn2 >= 1000) && (mtn1*3 < mtn2*2 || mtn2*3 < mtn1*2)) ||
        ((mtn1 >= 2000 || mtn2 >= 2000) && (mtn1*5 < mtn2*4 || mtn2*5 < mtn1*4)) ||
        ((mtn1 >= 4000 || mtn2 >= 4000) && c2 > c1))
        ret = mtn1 > mtn2 ? match2 : match1;
    else if (mr > 0.005 && FFMAX(mtn1, mtn2) > 150 && (mtn1*2 < mtn2*1 || mtn2*2 < mtn1*1))
        ret = mtn1 > mtn2 ? match2 : match1;
    else
        ret = norm1 > norm2 ? match2 : match1;
    return ret;
}

static void copy_fields(const FieldMatchContext *fm, AVFrame *dst,
                        const AVFrame *src, int field, int input)
{
    int plane;
    for (plane = 0; plane < 4 && src->data[plane] && src->linesize[plane]; plane++) {
        const int plane_h = get_height(fm, src, plane, input);
        const int nb_copy_fields = (plane_h >> 1) + (field ? 0 : (plane_h & 1));
        av_image_copy_plane(dst->data[plane] + field*dst->linesize[plane], dst->linesize[plane] << 1,
                            src->data[plane] + field*src->linesize[plane], src->linesize[plane] << 1,
                            get_width(fm, src, plane, input) * fm->bpc, nb_copy_fields);
    }
}

static AVFrame *create_weave_frame(AVFilterContext *ctx, int match, int field,
                                   const AVFrame *prv, AVFrame *src, const AVFrame *nxt, int input)
{
    AVFrame *dst;
    FieldMatchContext *fm = ctx->priv;

    if (match == mC) {
        dst = av_frame_clone(src);
    } else {
        AVFilterLink *link = input == INPUT_CLEANSRC ? ctx->outputs[0] : ctx->inputs[INPUT_MAIN];

        dst = ff_get_video_buffer(link, link->w, link->h);
        if (!dst)
            return NULL;
        av_frame_copy_props(dst, src);

        switch (match) {
        case mP: copy_fields(fm, dst, src, 1-field, input); copy_fields(fm, dst, prv,   field, input); break;
        case mN: copy_fields(fm, dst, src, 1-field, input); copy_fields(fm, dst, nxt,   field, input); break;
        case mB: copy_fields(fm, dst, src,   field, input); copy_fields(fm, dst, prv, 1-field, input); break;
        case mU: copy_fields(fm, dst, src,   field, input); copy_fields(fm, dst, nxt, 1-field, input); break;
        default: av_assert0(0);
        }
    }
    return dst;
}

static int checkmm(AVFilterContext *ctx, int *combs, int m1, int m2,
                   AVFrame **gen_frames, int field)
{
    const FieldMatchContext *fm = ctx->priv;

#define LOAD_COMB(mid) do {                                                     \
    if (combs[mid] < 0) {                                                       \
        if (!gen_frames[mid])                                                   \
            gen_frames[mid] = create_weave_frame(ctx, mid, field,               \
                                                 fm->prv, fm->src, fm->nxt,     \
                                                 INPUT_MAIN);                   \
        combs[mid] = calc_combed_score(fm, gen_frames[mid]);                    \
    }                                                                           \
} while (0)

    LOAD_COMB(m1);
    LOAD_COMB(m2);

    if ((combs[m2] * 3 < combs[m1] || (combs[m2] * 2 < combs[m1] && combs[m1] > fm->combpel)) &&
        abs(combs[m2] - combs[m1]) >= 30 && combs[m2] < fm->combpel)
        return m2;
    else
        return m1;
}

static const int fxo0m[] = { mP, mC, mN, mB, mU };
static const int fxo1m[] = { mN, mC, mP, mU, mB };

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx  = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    FilterLink      *outl = ff_filter_link(outlink);
    FieldMatchContext *fm = ctx->priv;
    int combs[] = { -1, -1, -1, -1, -1 };
    int order, field, i, match, interlaced_frame, sc = 0, ret = 0;
    const int *fxo;
    AVFrame *gen_frames[] = { NULL, NULL, NULL, NULL, NULL };
    AVFrame *dst = NULL;

    /* update frames queue(s) */
#define SLIDING_FRAME_WINDOW(prv, src, nxt) do {                \
        if (prv != src) /* 2nd loop exception (1st has prv==src and we don't want to loose src) */ \
            av_frame_free(&prv);                                \
        prv = src;                                              \
        src = nxt;                                              \
        if (in)                                                 \
            nxt = in;                                           \
        if (!prv)                                               \
            prv = src;                                          \
        if (!prv) /* received only one frame at that point */   \
            return 0;                                           \
        av_assert0(prv && src && nxt);                          \
} while (0)
    if (FF_INLINK_IDX(inlink) == INPUT_MAIN) {
        av_assert0(fm->got_frame[INPUT_MAIN] == 0);
        SLIDING_FRAME_WINDOW(fm->prv, fm->src, fm->nxt);
        fm->got_frame[INPUT_MAIN] = 1;
    } else {
        av_assert0(fm->got_frame[INPUT_CLEANSRC] == 0);
        SLIDING_FRAME_WINDOW(fm->prv2, fm->src2, fm->nxt2);
        fm->got_frame[INPUT_CLEANSRC] = 1;
    }
    if (!fm->got_frame[INPUT_MAIN] || (fm->ppsrc && !fm->got_frame[INPUT_CLEANSRC]))
        return 0;
    fm->got_frame[INPUT_MAIN] = fm->got_frame[INPUT_CLEANSRC] = 0;
    in = fm->src;

    /* parity */
    order = fm->order != FM_PARITY_AUTO ? fm->order : ((in->flags & AV_FRAME_FLAG_INTERLACED) ?
                                                       !!(in->flags & AV_FRAME_FLAG_TOP_FIELD_FIRST) : 1);
    field = fm->field != FM_PARITY_AUTO ? fm->field : order;
    av_assert0(order == 0 || order == 1 || field == 0 || field == 1);
    fxo = field ^ order ? fxo1m : fxo0m;

    /* debug mode: we generate all the fields combinations and their associated
     * combed score. XXX: inject as frame metadata? */
    if (fm->combdbg) {
        for (i = 0; i < FF_ARRAY_ELEMS(combs); i++) {
            if (i > mN && fm->combdbg == COMBDBG_PCN)
                break;
            gen_frames[i] = create_weave_frame(ctx, i, field, fm->prv, fm->src, fm->nxt, INPUT_MAIN);
            if (!gen_frames[i]) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }
            combs[i] = calc_combed_score(fm, gen_frames[i]);
        }
        av_log(ctx, AV_LOG_INFO, "COMBS: %3d %3d %3d %3d %3d\n",
               combs[0], combs[1], combs[2], combs[3], combs[4]);
    } else {
        gen_frames[mC] = av_frame_clone(fm->src);
        if (!gen_frames[mC]) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    }

    /* p/c selection and optional 3-way p/c/n matches */
    match = compare_fields(fm, fxo[mC], fxo[mP], field);
    if (fm->mode == MODE_PCN || fm->mode == MODE_PCN_UB)
        match = compare_fields(fm, match, fxo[mN], field);

    /* scene change check */
    if (fm->combmatch == COMBMATCH_SC) {
        if (fm->lastn == outl->frame_count_in - 1) {
            if (fm->lastscdiff > fm->scthresh)
                sc = 1;
        } else if (luma_abs_diff(fm->prv, fm->src) > fm->scthresh) {
            sc = 1;
        }

        if (!sc) {
            fm->lastn = outl->frame_count_in;
            fm->lastscdiff = luma_abs_diff(fm->src, fm->nxt);
            sc = fm->lastscdiff > fm->scthresh;
        }
    }

    if (fm->combmatch == COMBMATCH_FULL || (fm->combmatch == COMBMATCH_SC && sc)) {
        switch (fm->mode) {
        /* 2-way p/c matches */
        case MODE_PC:
            match = checkmm(ctx, combs, match, match == fxo[mP] ? fxo[mC] : fxo[mP], gen_frames, field);
            break;
        case MODE_PC_N:
            match = checkmm(ctx, combs, match, fxo[mN], gen_frames, field);
            break;
        case MODE_PC_U:
            match = checkmm(ctx, combs, match, fxo[mU], gen_frames, field);
            break;
        case MODE_PC_N_UB:
            match = checkmm(ctx, combs, match, fxo[mN], gen_frames, field);
            match = checkmm(ctx, combs, match, fxo[mU], gen_frames, field);
            match = checkmm(ctx, combs, match, fxo[mB], gen_frames, field);
            break;
        /* 3-way p/c/n matches */
        case MODE_PCN:
            match = checkmm(ctx, combs, match, match == fxo[mP] ? fxo[mC] : fxo[mP], gen_frames, field);
            break;
        case MODE_PCN_UB:
            match = checkmm(ctx, combs, match, fxo[mU], gen_frames, field);
            match = checkmm(ctx, combs, match, fxo[mB], gen_frames, field);
            break;
        default:
            av_assert0(0);
        }
    }

    /* keep fields as-is if not matched properly */
    interlaced_frame = combs[match] >= fm->combpel;
    if (interlaced_frame && fm->combmatch == COMBMATCH_FULL) {
        match = mC;
    }

    /* get output frame and drop the others */
    if (fm->ppsrc) {
        /* field matching was based on a filtered/post-processed input, we now
         * pick the untouched fields from the clean source */
        dst = create_weave_frame(ctx, match, field, fm->prv2, fm->src2, fm->nxt2, INPUT_CLEANSRC);
    } else {
        if (!gen_frames[match]) { // XXX: is that possible?
            dst = create_weave_frame(ctx, match, field, fm->prv, fm->src, fm->nxt, INPUT_MAIN);
        } else {
            dst = gen_frames[match];
            gen_frames[match] = NULL;
        }
    }
    if (!dst) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    /* mark the frame we are unable to match properly as interlaced so a proper
     * de-interlacer can take the relay */
    if (interlaced_frame) {
        dst->flags |= AV_FRAME_FLAG_INTERLACED;
        av_log(ctx, AV_LOG_WARNING, "Frame #%"PRId64" at %s is still interlaced\n",
               outl->frame_count_in, av_ts2timestr(in->pts, &inlink->time_base));
        if (field)
            dst->flags |= AV_FRAME_FLAG_TOP_FIELD_FIRST;
        else
            dst->flags &= ~AV_FRAME_FLAG_TOP_FIELD_FIRST;
    } else
        dst->flags &= ~AV_FRAME_FLAG_INTERLACED;

    av_log(ctx, AV_LOG_DEBUG, "SC:%d | COMBS: %3d %3d %3d %3d %3d (combpel=%d)"
           " match=%d combed=%s\n", sc, combs[0], combs[1], combs[2], combs[3], combs[4],
           fm->combpel, match, (dst->flags & AV_FRAME_FLAG_INTERLACED) ? "YES" : "NO");

fail:
    for (i = 0; i < FF_ARRAY_ELEMS(gen_frames); i++)
        av_frame_free(&gen_frames[i]);

    if (ret >= 0)
        return ff_filter_frame(outlink, dst);
    return ret;
}

static int activate(AVFilterContext *ctx)
{
    FieldMatchContext *fm = ctx->priv;
    AVFrame *frame = NULL;
    int ret = 0, status;
    int64_t pts;

    FF_FILTER_FORWARD_STATUS_BACK_ALL(ctx->outputs[0], ctx);

    if ((fm->got_frame[INPUT_MAIN] == 0) &&
        (ret = ff_inlink_consume_frame(ctx->inputs[INPUT_MAIN], &frame)) > 0) {
        ret = filter_frame(ctx->inputs[INPUT_MAIN], frame);
        if (ret < 0)
            return ret;
    }
    if (ret < 0)
        return ret;
    if (fm->ppsrc &&
        (fm->got_frame[INPUT_CLEANSRC] == 0) &&
        (ret = ff_inlink_consume_frame(ctx->inputs[INPUT_CLEANSRC], &frame)) > 0) {
        ret = filter_frame(ctx->inputs[INPUT_CLEANSRC], frame);
        if (ret < 0)
            return ret;
    }
    if (ret < 0) {
        return ret;
    } else if (ff_inlink_acknowledge_status(ctx->inputs[INPUT_MAIN], &status, &pts)) {
        if (status == AVERROR_EOF) { // flushing
            fm->eof |= 1 << INPUT_MAIN;
            ret = filter_frame(ctx->inputs[INPUT_MAIN], NULL);
        }
        ff_outlink_set_status(ctx->outputs[0], status, pts);
        return ret;
    } else if (fm->ppsrc && ff_inlink_acknowledge_status(ctx->inputs[INPUT_CLEANSRC], &status, &pts)) {
        if (status == AVERROR_EOF) { // flushing
            fm->eof |= 1 << INPUT_CLEANSRC;
            ret = filter_frame(ctx->inputs[INPUT_CLEANSRC], NULL);
        }
        ff_outlink_set_status(ctx->outputs[0], status, pts);
        return ret;
    } else {
        if (ff_outlink_frame_wanted(ctx->outputs[0])) {
            if (fm->got_frame[INPUT_MAIN] == 0)
                ff_inlink_request_frame(ctx->inputs[INPUT_MAIN]);
            if (fm->ppsrc && (fm->got_frame[INPUT_CLEANSRC] == 0))
                ff_inlink_request_frame(ctx->inputs[INPUT_CLEANSRC]);
        }
        return 0;
    }
}

static int query_formats(const AVFilterContext *ctx,
                         AVFilterFormatsConfig **cfg_in,
                         AVFilterFormatsConfig **cfg_out)
{
    const FieldMatchContext *fm = ctx->priv;

    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUV444P,  AV_PIX_FMT_YUV422P,  AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_YUV411P,  AV_PIX_FMT_YUV410P,
        AV_PIX_FMT_NONE
    };
    static const enum AVPixelFormat unproc_pix_fmts[] = {
        AV_PIX_FMT_YUV410P, AV_PIX_FMT_YUV411P,
        AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P,
        AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUV444P,
        AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ422P,
        AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_YUVJ444P,
        AV_PIX_FMT_YUVJ411P,
        AV_PIX_FMT_YUV420P9, AV_PIX_FMT_YUV422P9, AV_PIX_FMT_YUV444P9,
        AV_PIX_FMT_YUV420P10, AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV444P10,
        AV_PIX_FMT_YUV440P10,
        AV_PIX_FMT_YUV444P12, AV_PIX_FMT_YUV422P12, AV_PIX_FMT_YUV420P12,
        AV_PIX_FMT_YUV440P12,
        AV_PIX_FMT_YUV444P14, AV_PIX_FMT_YUV422P14, AV_PIX_FMT_YUV420P14,
        AV_PIX_FMT_YUV420P16, AV_PIX_FMT_YUV422P16, AV_PIX_FMT_YUV444P16,
        AV_PIX_FMT_NONE
    };
    int ret;

    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    if (!fm->ppsrc) {
        return ff_set_common_formats2(ctx, cfg_in, cfg_out, fmts_list);
    }

    if ((ret = ff_formats_ref(fmts_list, &cfg_in[INPUT_MAIN]->formats)) < 0)
        return ret;
    fmts_list = ff_make_format_list(unproc_pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    if ((ret = ff_formats_ref(fmts_list, &cfg_out[0]->formats)) < 0)
        return ret;
    if ((ret = ff_formats_ref(fmts_list, &cfg_in[INPUT_CLEANSRC]->formats)) < 0)
        return ret;
    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    int ret;
    AVFilterContext *ctx = inlink->dst;
    FieldMatchContext *fm = ctx->priv;
    const AVPixFmtDescriptor *pix_desc = av_pix_fmt_desc_get(inlink->format);
    const int w = inlink->w;
    const int h = inlink->h;

    fm->scthresh = (int64_t)((w * h * 255.0 * fm->scthresh_flt) / 100.0);

    if ((ret = av_image_alloc(fm->map_data,   fm->map_linesize,   w, h, inlink->format, 32)) < 0 ||
        (ret = av_image_alloc(fm->cmask_data, fm->cmask_linesize, w, h, inlink->format, 32)) < 0)
        return ret;

    fm->hsub[INPUT_MAIN] = pix_desc->log2_chroma_w;
    fm->vsub[INPUT_MAIN] = pix_desc->log2_chroma_h;
    if (fm->ppsrc) {
        pix_desc = av_pix_fmt_desc_get(ctx->inputs[INPUT_CLEANSRC]->format);
        fm->hsub[INPUT_CLEANSRC] = pix_desc->log2_chroma_w;
        fm->vsub[INPUT_CLEANSRC] = pix_desc->log2_chroma_h;
    }

    fm->tpitchy  = FFALIGN(w,      16);
    fm->tpitchuv = FFALIGN(w >> 1, 16);

    fm->tbuffer = av_calloc((h/2 + 4) * fm->tpitchy, sizeof(*fm->tbuffer));
    fm->c_array = av_malloc_array((((w + fm->blockx/2)/fm->blockx)+1) *
                            (((h + fm->blocky/2)/fm->blocky)+1),
                            4 * sizeof(*fm->c_array));
    if (!fm->tbuffer || !fm->c_array)
        return AVERROR(ENOMEM);

    return 0;
}

static av_cold int fieldmatch_init(AVFilterContext *ctx)
{
    const FieldMatchContext *fm = ctx->priv;
    AVFilterPad pad = {
        .name         = "main",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
    };
    int ret;

    if ((ret = ff_append_inpad(ctx, &pad)) < 0)
        return ret;

    if (fm->ppsrc) {
        pad.name = "clean_src";
        pad.config_props = NULL;
        if ((ret = ff_append_inpad(ctx, &pad)) < 0)
            return ret;
    }

    if ((fm->blockx & (fm->blockx - 1)) ||
        (fm->blocky & (fm->blocky - 1))) {
        av_log(ctx, AV_LOG_ERROR, "blockx and blocky settings must be power of two\n");
        return AVERROR(EINVAL);
    }

    if (fm->combpel > fm->blockx * fm->blocky) {
        av_log(ctx, AV_LOG_ERROR, "Combed pixel should not be larger than blockx x blocky\n");
        return AVERROR(EINVAL);
    }

    return 0;
}

static av_cold void fieldmatch_uninit(AVFilterContext *ctx)
{
    FieldMatchContext *fm = ctx->priv;

    if (fm->prv != fm->src)
        av_frame_free(&fm->prv);
    if (fm->nxt != fm->src)
        av_frame_free(&fm->nxt);
    if (fm->prv2 != fm->src2)
        av_frame_free(&fm->prv2);
    if (fm->nxt2 != fm->src2)
        av_frame_free(&fm->nxt2);
    av_frame_free(&fm->src);
    av_frame_free(&fm->src2);
    av_freep(&fm->map_data[0]);
    av_freep(&fm->cmask_data[0]);
    av_freep(&fm->tbuffer);
    av_freep(&fm->c_array);
}

static int config_output(AVFilterLink *outlink)
{
    FilterLink     *outl  = ff_filter_link(outlink);
    AVFilterContext *ctx  = outlink->src;
    FieldMatchContext *fm = ctx->priv;
    const AVFilterLink *inlink =
        ctx->inputs[fm->ppsrc ? INPUT_CLEANSRC : INPUT_MAIN];
    FilterLink *inl = ff_filter_link(ctx->inputs[fm->ppsrc ? INPUT_CLEANSRC : INPUT_MAIN]);
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);

    fm->bpc = (desc->comp[0].depth + 7) / 8;
    outlink->time_base = inlink->time_base;
    outlink->sample_aspect_ratio = inlink->sample_aspect_ratio;
    outl->frame_rate = inl->frame_rate;
    outlink->w = inlink->w;
    outlink->h = inlink->h;
    return 0;
}

static const AVFilterPad fieldmatch_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
};

const FFFilter ff_vf_fieldmatch = {
    .p.name         = "fieldmatch",
    .p.description  = NULL_IF_CONFIG_SMALL("Field matching for inverse telecine."),
    .p.priv_class   = &fieldmatch_class,
    .p.flags        = AVFILTER_FLAG_DYNAMIC_INPUTS,
    .priv_size      = sizeof(FieldMatchContext),
    .init           = fieldmatch_init,
    .activate       = activate,
    .uninit         = fieldmatch_uninit,
    FILTER_OUTPUTS(fieldmatch_outputs),
    FILTER_QUERY_FUNC2(query_formats),
};
