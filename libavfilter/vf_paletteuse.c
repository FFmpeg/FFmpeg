/*
 * Copyright (c) 2015 Stupeflix
 * Copyright (c) 2022 Clément Bœsch <u pkh me>
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
 * Use a palette to downsample an input video stream.
 */

#include "libavutil/bprint.h"
#include "libavutil/file_open.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "libavutil/qsort.h"
#include "avfilter.h"
#include "filters.h"
#include "framesync.h"
#include "internal.h"
#include "palette.h"

enum dithering_mode {
    DITHERING_NONE,
    DITHERING_BAYER,
    DITHERING_HECKBERT,
    DITHERING_FLOYD_STEINBERG,
    DITHERING_SIERRA2,
    DITHERING_SIERRA2_4A,
    DITHERING_SIERRA3,
    DITHERING_BURKES,
    DITHERING_ATKINSON,
    NB_DITHERING
};

enum diff_mode {
    DIFF_MODE_NONE,
    DIFF_MODE_RECTANGLE,
    NB_DIFF_MODE
};

struct color_info {
    uint32_t srgb;
    int32_t lab[3];
};

struct color_node {
    struct color_info c;
    uint8_t palette_id;
    int split;
    int left_id, right_id;
};

#define CACHE_SIZE (1<<15)

struct cached_color {
    uint32_t color;
    uint8_t pal_entry;
};

struct cache_node {
    struct cached_color *entries;
    int nb_entries;
};

struct PaletteUseContext;

typedef int (*set_frame_func)(struct PaletteUseContext *s, AVFrame *out, AVFrame *in,
                              int x_start, int y_start, int width, int height);

typedef struct PaletteUseContext {
    const AVClass *class;
    FFFrameSync fs;
    struct cache_node cache[CACHE_SIZE];    /* lookup cache */
    struct color_node map[AVPALETTE_COUNT]; /* 3D-Tree (KD-Tree with K=3) for reverse colormap */
    uint32_t palette[AVPALETTE_COUNT];
    int transparency_index; /* index in the palette of transparency. -1 if there is no transparency in the palette. */
    int trans_thresh;
    int palette_loaded;
    int dither;
    int new;
    set_frame_func set_frame;
    int bayer_scale;
    int ordered_dither[8*8];
    int diff_mode;
    AVFrame *last_in;
    AVFrame *last_out;

    /* debug options */
    char *dot_filename;
    int calc_mean_err;
    uint64_t total_mean_err;
} PaletteUseContext;

#define OFFSET(x) offsetof(PaletteUseContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption paletteuse_options[] = {
    { "dither", "select dithering mode", OFFSET(dither), AV_OPT_TYPE_INT, {.i64=DITHERING_SIERRA2_4A}, 0, NB_DITHERING-1, FLAGS, "dithering_mode" },
        { "bayer",           "ordered 8x8 bayer dithering (deterministic)",                            0, AV_OPT_TYPE_CONST, {.i64=DITHERING_BAYER},           INT_MIN, INT_MAX, FLAGS, "dithering_mode" },
        { "heckbert",        "dithering as defined by Paul Heckbert in 1982 (simple error diffusion)", 0, AV_OPT_TYPE_CONST, {.i64=DITHERING_HECKBERT},        INT_MIN, INT_MAX, FLAGS, "dithering_mode" },
        { "floyd_steinberg", "Floyd and Steingberg dithering (error diffusion)",                       0, AV_OPT_TYPE_CONST, {.i64=DITHERING_FLOYD_STEINBERG}, INT_MIN, INT_MAX, FLAGS, "dithering_mode" },
        { "sierra2",         "Frankie Sierra dithering v2 (error diffusion)",                          0, AV_OPT_TYPE_CONST, {.i64=DITHERING_SIERRA2},         INT_MIN, INT_MAX, FLAGS, "dithering_mode" },
        { "sierra2_4a",      "Frankie Sierra dithering v2 \"Lite\" (error diffusion)",                 0, AV_OPT_TYPE_CONST, {.i64=DITHERING_SIERRA2_4A},      INT_MIN, INT_MAX, FLAGS, "dithering_mode" },
        { "sierra3",         "Frankie Sierra dithering v3 (error diffusion)",                          0, AV_OPT_TYPE_CONST, {.i64=DITHERING_SIERRA3},         INT_MIN, INT_MAX, FLAGS, "dithering_mode" },
        { "burkes",          "Burkes dithering (error diffusion)",                                     0, AV_OPT_TYPE_CONST, {.i64=DITHERING_BURKES},          INT_MIN, INT_MAX, FLAGS, "dithering_mode" },
        { "atkinson",        "Atkinson dithering by Bill Atkinson at Apple Computer (error diffusion)",0, AV_OPT_TYPE_CONST, {.i64=DITHERING_ATKINSON},        INT_MIN, INT_MAX, FLAGS, "dithering_mode" },
    { "bayer_scale", "set scale for bayer dithering", OFFSET(bayer_scale), AV_OPT_TYPE_INT, {.i64=2}, 0, 5, FLAGS },
    { "diff_mode",   "set frame difference mode",     OFFSET(diff_mode),   AV_OPT_TYPE_INT, {.i64=DIFF_MODE_NONE}, 0, NB_DIFF_MODE-1, FLAGS, "diff_mode" },
        { "rectangle", "process smallest different rectangle", 0, AV_OPT_TYPE_CONST, {.i64=DIFF_MODE_RECTANGLE}, INT_MIN, INT_MAX, FLAGS, "diff_mode" },
    { "new", "take new palette for each output frame", OFFSET(new), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS },
    { "alpha_threshold", "set the alpha threshold for transparency", OFFSET(trans_thresh), AV_OPT_TYPE_INT, {.i64=128}, 0, 255, FLAGS },

    /* following are the debug options, not part of the official API */
    { "debug_kdtree", "save Graphviz graph of the kdtree in specified file", OFFSET(dot_filename), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(paletteuse);

static int load_apply_palette(FFFrameSync *fs);

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat in_fmts[]    = {AV_PIX_FMT_RGB32, AV_PIX_FMT_NONE};
    static const enum AVPixelFormat inpal_fmts[] = {AV_PIX_FMT_RGB32, AV_PIX_FMT_NONE};
    static const enum AVPixelFormat out_fmts[]   = {AV_PIX_FMT_PAL8,  AV_PIX_FMT_NONE};
    int ret;
    if ((ret = ff_formats_ref(ff_make_format_list(in_fmts),
                              &ctx->inputs[0]->outcfg.formats)) < 0 ||
        (ret = ff_formats_ref(ff_make_format_list(inpal_fmts),
                              &ctx->inputs[1]->outcfg.formats)) < 0 ||
        (ret = ff_formats_ref(ff_make_format_list(out_fmts),
                              &ctx->outputs[0]->incfg.formats)) < 0)
        return ret;
    return 0;
}

static av_always_inline uint32_t dither_color(uint32_t px, int er, int eg,
                                              int eb, int scale, int shift)
{
    return (px & 0xff000000)
         | av_clip_uint8((px >> 16 & 0xff) + ((er * scale) / (1<<shift))) << 16
         | av_clip_uint8((px >>  8 & 0xff) + ((eg * scale) / (1<<shift))) <<  8
         | av_clip_uint8((px       & 0xff) + ((eb * scale) / (1<<shift)));
}

static av_always_inline int diff(const struct color_info *a, const struct color_info *b, const int trans_thresh)
{
    const uint8_t alpha_a = a->srgb >> 24;
    const uint8_t alpha_b = b->srgb >> 24;

    if (alpha_a < trans_thresh && alpha_b < trans_thresh) {
        return 0;
    } else if (alpha_a >= trans_thresh && alpha_b >= trans_thresh) {
        const int64_t dL = a->lab[0] - b->lab[0];
        const int64_t da = a->lab[1] - b->lab[1];
        const int64_t db = a->lab[2] - b->lab[2];
        const int64_t ret = dL*dL + da*da + db*db;
        return FFMIN(ret, INT32_MAX - 1);
    } else {
        return INT32_MAX - 1;
    }
}

static struct color_info get_color_from_srgb(uint32_t srgb)
{
    const struct Lab lab = ff_srgb_u8_to_oklab_int(srgb);
    struct color_info ret = {.srgb=srgb, .lab={lab.L, lab.a, lab.b}};
    return ret;
}

struct nearest_color {
    int node_pos;
    int64_t dist_sqd;
};

static void colormap_nearest_node(const struct color_node *map,
                                  const int node_pos,
                                  const struct color_info *target,
                                  const int trans_thresh,
                                  struct nearest_color *nearest)
{
    const struct color_node *kd = map + node_pos;
    int nearer_kd_id, further_kd_id;
    const struct color_info *current = &kd->c;
    const int64_t current_to_target = diff(target, current, trans_thresh);

    if (current_to_target < nearest->dist_sqd) {
        nearest->node_pos = node_pos;
        nearest->dist_sqd = current_to_target;
    }

    if (kd->left_id != -1 || kd->right_id != -1) {
        const int64_t dx = target->lab[kd->split] - current->lab[kd->split];

        if (dx <= 0) nearer_kd_id = kd->left_id,  further_kd_id = kd->right_id;
        else         nearer_kd_id = kd->right_id, further_kd_id = kd->left_id;

        if (nearer_kd_id != -1)
            colormap_nearest_node(map, nearer_kd_id, target, trans_thresh, nearest);

        if (further_kd_id != -1 && dx*dx < nearest->dist_sqd)
            colormap_nearest_node(map, further_kd_id, target, trans_thresh, nearest);
    }
}

static av_always_inline uint8_t colormap_nearest(const struct color_node *node, const struct color_info *target, const int trans_thresh)
{
    struct nearest_color res = {.dist_sqd = INT_MAX, .node_pos = -1};
    colormap_nearest_node(node, 0, target, trans_thresh, &res);
    return node[res.node_pos].palette_id;
}

struct stack_node {
    int color_id;
    int dx2;
};

/**
 * Check if the requested color is in the cache already. If not, find it in the
 * color tree and cache it.
 */
static av_always_inline int color_get(PaletteUseContext *s, uint32_t color)
{
    struct color_info clrinfo;
    const uint32_t hash = ff_lowbias32(color) & (CACHE_SIZE - 1);
    struct cache_node *node = &s->cache[hash];
    struct cached_color *e;

    // first, check for transparency
    if (color>>24 < s->trans_thresh && s->transparency_index >= 0) {
        return s->transparency_index;
    }

    for (int i = 0; i < node->nb_entries; i++) {
        e = &node->entries[i];
        if (e->color == color)
            return e->pal_entry;
    }

    e = av_dynarray2_add((void**)&node->entries, &node->nb_entries,
                         sizeof(*node->entries), NULL);
    if (!e)
        return AVERROR(ENOMEM);
    e->color = color;
    clrinfo = get_color_from_srgb(color);
    e->pal_entry = colormap_nearest(s->map, &clrinfo, s->trans_thresh);

    return e->pal_entry;
}

static av_always_inline int get_dst_color_err(PaletteUseContext *s,
                                              uint32_t c, int *er, int *eg, int *eb)
{
    uint32_t dstc;
    const int dstx = color_get(s, c);
    if (dstx < 0)
        return dstx;
    dstc = s->palette[dstx];
    if (dstx == s->transparency_index) {
        *er = *eg = *eb = 0;
    } else {
        const uint8_t r = c >> 16 & 0xff;
        const uint8_t g = c >>  8 & 0xff;
        const uint8_t b = c       & 0xff;
        *er = (int)r - (int)(dstc >> 16 & 0xff);
        *eg = (int)g - (int)(dstc >>  8 & 0xff);
        *eb = (int)b - (int)(dstc       & 0xff);
    }
    return dstx;
}

static av_always_inline int set_frame(PaletteUseContext *s, AVFrame *out, AVFrame *in,
                                      int x_start, int y_start, int w, int h,
                                      enum dithering_mode dither)
{
    const int src_linesize = in ->linesize[0] >> 2;
    const int dst_linesize = out->linesize[0];
    uint32_t *src = ((uint32_t *)in ->data[0]) + y_start*src_linesize;
    uint8_t  *dst =              out->data[0]  + y_start*dst_linesize;

    w += x_start;
    h += y_start;

    for (int y = y_start; y < h; y++) {
        for (int x = x_start; x < w; x++) {
            int er, eg, eb;

            if (dither == DITHERING_BAYER) {
                const int d = s->ordered_dither[(y & 7)<<3 | (x & 7)];
                const uint8_t a8 = src[x] >> 24;
                const uint8_t r8 = src[x] >> 16 & 0xff;
                const uint8_t g8 = src[x] >>  8 & 0xff;
                const uint8_t b8 = src[x]       & 0xff;
                const uint8_t r = av_clip_uint8(r8 + d);
                const uint8_t g = av_clip_uint8(g8 + d);
                const uint8_t b = av_clip_uint8(b8 + d);
                const uint32_t color_new = (unsigned)(a8) << 24 | r << 16 | g << 8 | b;
                const int color = color_get(s, color_new);

                if (color < 0)
                    return color;
                dst[x] = color;

            } else if (dither == DITHERING_HECKBERT) {
                const int right = x < w - 1, down = y < h - 1;
                const int color = get_dst_color_err(s, src[x], &er, &eg, &eb);

                if (color < 0)
                    return color;
                dst[x] = color;

                if (right)         src[               x + 1] = dither_color(src[               x + 1], er, eg, eb, 3, 3);
                if (         down) src[src_linesize + x    ] = dither_color(src[src_linesize + x    ], er, eg, eb, 3, 3);
                if (right && down) src[src_linesize + x + 1] = dither_color(src[src_linesize + x + 1], er, eg, eb, 2, 3);

            } else if (dither == DITHERING_FLOYD_STEINBERG) {
                const int right = x < w - 1, down = y < h - 1, left = x > x_start;
                const int color = get_dst_color_err(s, src[x], &er, &eg, &eb);

                if (color < 0)
                    return color;
                dst[x] = color;

                if (right)         src[               x + 1] = dither_color(src[               x + 1], er, eg, eb, 7, 4);
                if (left  && down) src[src_linesize + x - 1] = dither_color(src[src_linesize + x - 1], er, eg, eb, 3, 4);
                if (         down) src[src_linesize + x    ] = dither_color(src[src_linesize + x    ], er, eg, eb, 5, 4);
                if (right && down) src[src_linesize + x + 1] = dither_color(src[src_linesize + x + 1], er, eg, eb, 1, 4);

            } else if (dither == DITHERING_SIERRA2) {
                const int right  = x < w - 1, down  = y < h - 1, left  = x > x_start;
                const int right2 = x < w - 2,                    left2 = x > x_start + 1;
                const int color = get_dst_color_err(s, src[x], &er, &eg, &eb);

                if (color < 0)
                    return color;
                dst[x] = color;

                if (right)          src[                 x + 1] = dither_color(src[                 x + 1], er, eg, eb, 4, 4);
                if (right2)         src[                 x + 2] = dither_color(src[                 x + 2], er, eg, eb, 3, 4);

                if (down) {
                    if (left2)      src[  src_linesize + x - 2] = dither_color(src[  src_linesize + x - 2], er, eg, eb, 1, 4);
                    if (left)       src[  src_linesize + x - 1] = dither_color(src[  src_linesize + x - 1], er, eg, eb, 2, 4);
                    if (1)          src[  src_linesize + x    ] = dither_color(src[  src_linesize + x    ], er, eg, eb, 3, 4);
                    if (right)      src[  src_linesize + x + 1] = dither_color(src[  src_linesize + x + 1], er, eg, eb, 2, 4);
                    if (right2)     src[  src_linesize + x + 2] = dither_color(src[  src_linesize + x + 2], er, eg, eb, 1, 4);
                }

            } else if (dither == DITHERING_SIERRA2_4A) {
                const int right = x < w - 1, down = y < h - 1, left = x > x_start;
                const int color = get_dst_color_err(s, src[x], &er, &eg, &eb);

                if (color < 0)
                    return color;
                dst[x] = color;

                if (right)         src[               x + 1] = dither_color(src[               x + 1], er, eg, eb, 2, 2);
                if (left  && down) src[src_linesize + x - 1] = dither_color(src[src_linesize + x - 1], er, eg, eb, 1, 2);
                if (         down) src[src_linesize + x    ] = dither_color(src[src_linesize + x    ], er, eg, eb, 1, 2);

            } else if (dither == DITHERING_SIERRA3) {
                const int right  = x < w - 1, down  = y < h - 1, left  = x > x_start;
                const int right2 = x < w - 2, down2 = y < h - 2, left2 = x > x_start + 1;
                const int color = get_dst_color_err(s, src[x], &er, &eg, &eb);

                if (color < 0)
                    return color;
                dst[x] = color;

                if (right)         src[                 x + 1] = dither_color(src[                 x + 1], er, eg, eb, 5, 5);
                if (right2)        src[                 x + 2] = dither_color(src[                 x + 2], er, eg, eb, 3, 5);

                if (down) {
                    if (left2)     src[src_linesize   + x - 2] = dither_color(src[src_linesize   + x - 2], er, eg, eb, 2, 5);
                    if (left)      src[src_linesize   + x - 1] = dither_color(src[src_linesize   + x - 1], er, eg, eb, 4, 5);
                    if (1)         src[src_linesize   + x    ] = dither_color(src[src_linesize   + x    ], er, eg, eb, 5, 5);
                    if (right)     src[src_linesize   + x + 1] = dither_color(src[src_linesize   + x + 1], er, eg, eb, 4, 5);
                    if (right2)    src[src_linesize   + x + 2] = dither_color(src[src_linesize   + x + 2], er, eg, eb, 2, 5);

                    if (down2) {
                        if (left)  src[src_linesize*2 + x - 1] = dither_color(src[src_linesize*2 + x - 1], er, eg, eb, 2, 5);
                        if (1)     src[src_linesize*2 + x    ] = dither_color(src[src_linesize*2 + x    ], er, eg, eb, 3, 5);
                        if (right) src[src_linesize*2 + x + 1] = dither_color(src[src_linesize*2 + x + 1], er, eg, eb, 2, 5);
                    }
                }

            } else if (dither == DITHERING_BURKES) {
                const int right  = x < w - 1, down  = y < h - 1, left  = x > x_start;
                const int right2 = x < w - 2,                    left2 = x > x_start + 1;
                const int color = get_dst_color_err(s, src[x], &er, &eg, &eb);

                if (color < 0)
                    return color;
                dst[x] = color;

                if (right)      src[                 x + 1] = dither_color(src[                 x + 1], er, eg, eb, 8, 5);
                if (right2)     src[                 x + 2] = dither_color(src[                 x + 2], er, eg, eb, 4, 5);

                if (down) {
                    if (left2)  src[src_linesize   + x - 2] = dither_color(src[src_linesize   + x - 2], er, eg, eb, 2, 5);
                    if (left)   src[src_linesize   + x - 1] = dither_color(src[src_linesize   + x - 1], er, eg, eb, 4, 5);
                    if (1)      src[src_linesize   + x    ] = dither_color(src[src_linesize   + x    ], er, eg, eb, 8, 5);
                    if (right)  src[src_linesize   + x + 1] = dither_color(src[src_linesize   + x + 1], er, eg, eb, 4, 5);
                    if (right2) src[src_linesize   + x + 2] = dither_color(src[src_linesize   + x + 2], er, eg, eb, 2, 5);
                }

            } else if (dither == DITHERING_ATKINSON) {
                const int right  = x < w - 1, down  = y < h - 1, left = x > x_start;
                const int right2 = x < w - 2, down2 = y < h - 2;
                const int color = get_dst_color_err(s, src[x], &er, &eg, &eb);

                if (color < 0)
                    return color;
                dst[x] = color;

                if (right)     src[                 x + 1] = dither_color(src[                 x + 1], er, eg, eb, 1, 3);
                if (right2)    src[                 x + 2] = dither_color(src[                 x + 2], er, eg, eb, 1, 3);

                if (down) {
                    if (left)  src[src_linesize   + x - 1] = dither_color(src[src_linesize   + x - 1], er, eg, eb, 1, 3);
                    if (1)     src[src_linesize   + x    ] = dither_color(src[src_linesize   + x    ], er, eg, eb, 1, 3);
                    if (right) src[src_linesize   + x + 1] = dither_color(src[src_linesize   + x + 1], er, eg, eb, 1, 3);
                    if (down2) src[src_linesize*2 + x    ] = dither_color(src[src_linesize*2 + x    ], er, eg, eb, 1, 3);
                }

            } else {
                const int color = color_get(s, src[x]);

                if (color < 0)
                    return color;
                dst[x] = color;
            }
        }
        src += src_linesize;
        dst += dst_linesize;
    }
    return 0;
}

#define INDENT 4
static void disp_node(AVBPrint *buf,
                      const struct color_node *map,
                      int parent_id, int node_id,
                      int depth)
{
    const struct color_node *node = &map[node_id];
    const uint32_t fontcolor = node->c.lab[0] > 0x7fff ? 0 : 0xffffff;
    const int lab_comp = node->split;
    av_bprintf(buf, "%*cnode%d ["
               "label=\"%c%d%c%d%c%d%c\" "
               "fillcolor=\"#%06"PRIX32"\" "
               "fontcolor=\"#%06"PRIX32"\"]\n",
               depth*INDENT, ' ', node->palette_id,
               "[  "[lab_comp], node->c.lab[0],
               "][ "[lab_comp], node->c.lab[1],
               " ]["[lab_comp], node->c.lab[2],
               "  ]"[lab_comp],
               node->c.srgb & 0xffffff,
               fontcolor);
    if (parent_id != -1)
        av_bprintf(buf, "%*cnode%d -> node%d\n", depth*INDENT, ' ',
                   map[parent_id].palette_id, node->palette_id);
    if (node->left_id  != -1) disp_node(buf, map, node_id, node->left_id,  depth + 1);
    if (node->right_id != -1) disp_node(buf, map, node_id, node->right_id, depth + 1);
}

// debug_kdtree=kdtree.dot -> dot -Tpng kdtree.dot > kdtree.png
static int disp_tree(const struct color_node *node, const char *fname)
{
    AVBPrint buf;
    FILE *f = avpriv_fopen_utf8(fname, "w");

    if (!f) {
        int ret = AVERROR(errno);
        av_log(NULL, AV_LOG_ERROR, "Cannot open file '%s' for writing: %s\n",
               fname, av_err2str(ret));
        return ret;
    }

    av_bprint_init(&buf, 0, AV_BPRINT_SIZE_UNLIMITED);

    av_bprintf(&buf, "digraph {\n");
    av_bprintf(&buf, "    node [style=filled fontsize=10 shape=box]\n");
    disp_node(&buf, node, -1, 0, 0);
    av_bprintf(&buf, "}\n");

    fwrite(buf.str, 1, buf.len, f);
    fclose(f);
    av_bprint_finalize(&buf, NULL);
    return 0;
}

struct color {
    struct Lab value;
    uint8_t pal_id;
};

struct color_rect {
    int32_t min[3];
    int32_t max[3];
};

typedef int (*cmp_func)(const void *, const void *);

#define DECLARE_CMP_FUNC(name)                          \
static int cmp_##name(const void *pa, const void *pb)   \
{                                                       \
    const struct color *a = pa;                         \
    const struct color *b = pb;                         \
    return FFDIFFSIGN(a->value.name, b->value.name);    \
}

DECLARE_CMP_FUNC(L)
DECLARE_CMP_FUNC(a)
DECLARE_CMP_FUNC(b)

static const cmp_func cmp_funcs[] = {cmp_L, cmp_a, cmp_b};

static int get_next_color(const uint8_t *color_used, const uint32_t *palette,
                          int *component, const struct color_rect *box)
{
    int wL, wa, wb;
    int longest = 0;
    unsigned nb_color = 0;
    struct color_rect ranges;
    struct color tmp_pal[256];
    cmp_func cmpf;

    ranges.min[0] = ranges.min[1] = ranges.min[2] = 0xffff;
    ranges.max[0] = ranges.max[1] = ranges.max[2] = -0xffff;

    for (int i = 0; i < AVPALETTE_COUNT; i++) {
        const uint32_t c = palette[i];
        const uint8_t a = c >> 24;
        const struct Lab lab = ff_srgb_u8_to_oklab_int(c);

        if (color_used[i] || (a != 0xff) ||
            lab.L < box->min[0] || lab.a < box->min[1] || lab.b < box->min[2] ||
            lab.L > box->max[0] || lab.a > box->max[1] || lab.b > box->max[2])
            continue;

        if (lab.L < ranges.min[0]) ranges.min[0] = lab.L;
        if (lab.a < ranges.min[1]) ranges.min[1] = lab.a;
        if (lab.b < ranges.min[2]) ranges.min[2] = lab.b;

        if (lab.L > ranges.max[0]) ranges.max[0] = lab.L;
        if (lab.a > ranges.max[1]) ranges.max[1] = lab.a;
        if (lab.b > ranges.max[2]) ranges.max[2] = lab.b;

        tmp_pal[nb_color].value  = lab;
        tmp_pal[nb_color].pal_id = i;

        nb_color++;
    }

    if (!nb_color)
        return -1;

    /* define longest axis that will be the split component */
    wL = ranges.max[0] - ranges.min[0];
    wa = ranges.max[1] - ranges.min[1];
    wb = ranges.max[2] - ranges.min[2];
    if (wb >= wL && wb >= wa) longest = 2;
    if (wa >= wL && wa >= wb) longest = 1;
    if (wL >= wa && wL >= wb) longest = 0;
    cmpf = cmp_funcs[longest];
    *component = longest;

    /* sort along this axis to get median */
    AV_QSORT(tmp_pal, nb_color, struct color, cmpf);

    return tmp_pal[nb_color >> 1].pal_id;
}

static int colormap_insert(struct color_node *map,
                           uint8_t *color_used,
                           int *nb_used,
                           const uint32_t *palette,
                           const int trans_thresh,
                           const struct color_rect *box)
{
    int component, cur_id;
    int comp_value;
    int node_left_id = -1, node_right_id = -1;
    struct color_node *node;
    struct color_rect box1, box2;
    const int pal_id = get_next_color(color_used, palette, &component, box);

    if (pal_id < 0)
        return -1;

    /* create new node with that color */
    cur_id = (*nb_used)++;
    node = &map[cur_id];
    node->split = component;
    node->palette_id = pal_id;
    node->c = get_color_from_srgb(palette[pal_id]);

    color_used[pal_id] = 1;

    /* get the two boxes this node creates */
    box1 = box2 = *box;
    comp_value = node->c.lab[component];
    box1.max[component] = comp_value;
    box2.min[component] = FFMIN(comp_value + 1, 0xffff);

    node_left_id = colormap_insert(map, color_used, nb_used, palette, trans_thresh, &box1);

    if (box2.min[component] <= box2.max[component])
        node_right_id = colormap_insert(map, color_used, nb_used, palette, trans_thresh, &box2);

    node->left_id  = node_left_id;
    node->right_id = node_right_id;

    return cur_id;
}

static int cmp_pal_entry(const void *a, const void *b)
{
    const int c1 = *(const uint32_t *)a & 0xffffff;
    const int c2 = *(const uint32_t *)b & 0xffffff;
    return c1 - c2;
}

static void load_colormap(PaletteUseContext *s)
{
    int nb_used = 0;
    uint8_t color_used[AVPALETTE_COUNT] = {0};
    uint32_t last_color = 0;
    struct color_rect box;

    if (s->transparency_index >= 0) {
        FFSWAP(uint32_t, s->palette[s->transparency_index], s->palette[255]);
    }

    /* disable transparent colors and dups */
    qsort(s->palette, AVPALETTE_COUNT-(s->transparency_index >= 0), sizeof(*s->palette), cmp_pal_entry);

    for (int i = 0; i < AVPALETTE_COUNT; i++) {
        const uint32_t c = s->palette[i];
        if (i != 0 && c == last_color) {
            color_used[i] = 1;
            continue;
        }
        last_color = c;
        if (c >> 24 < s->trans_thresh) {
            color_used[i] = 1; // ignore transparent color(s)
            continue;
        }
    }

    box.min[0] = box.min[1] = box.min[2] = -0xffff;
    box.max[0] = box.max[1] = box.max[2] = 0xffff;

    colormap_insert(s->map, color_used, &nb_used, s->palette, s->trans_thresh, &box);

    if (s->dot_filename)
        disp_tree(s->map, s->dot_filename);
}

static void set_processing_window(enum diff_mode diff_mode,
                                  const AVFrame *prv_src, const AVFrame *cur_src,
                                  const AVFrame *prv_dst,       AVFrame *cur_dst,
                                  int *xp, int *yp, int *wp, int *hp)
{
    int x_start = 0, y_start = 0;
    int width  = cur_src->width;
    int height = cur_src->height;

    if (prv_src->data[0] && diff_mode == DIFF_MODE_RECTANGLE) {
        int y;
        int x_end = cur_src->width  - 1,
            y_end = cur_src->height - 1;
        const uint32_t *prv_srcp = (const uint32_t *)prv_src->data[0];
        const uint32_t *cur_srcp = (const uint32_t *)cur_src->data[0];
        const uint8_t  *prv_dstp = prv_dst->data[0];
        uint8_t        *cur_dstp = cur_dst->data[0];

        const int prv_src_linesize = prv_src->linesize[0] >> 2;
        const int cur_src_linesize = cur_src->linesize[0] >> 2;
        const int prv_dst_linesize = prv_dst->linesize[0];
        const int cur_dst_linesize = cur_dst->linesize[0];

        /* skip common lines */
        while (y_start < y_end && !memcmp(prv_srcp + y_start*prv_src_linesize,
                                          cur_srcp + y_start*cur_src_linesize,
                                          cur_src->width * 4)) {
            memcpy(cur_dstp + y_start*cur_dst_linesize,
                   prv_dstp + y_start*prv_dst_linesize,
                   cur_dst->width);
            y_start++;
        }
        while (y_end > y_start && !memcmp(prv_srcp + y_end*prv_src_linesize,
                                          cur_srcp + y_end*cur_src_linesize,
                                          cur_src->width * 4)) {
            memcpy(cur_dstp + y_end*cur_dst_linesize,
                   prv_dstp + y_end*prv_dst_linesize,
                   cur_dst->width);
            y_end--;
        }

        height = y_end + 1 - y_start;

        /* skip common columns */
        while (x_start < x_end) {
            int same_column = 1;
            for (y = y_start; y <= y_end; y++) {
                if (prv_srcp[y*prv_src_linesize + x_start] != cur_srcp[y*cur_src_linesize + x_start]) {
                    same_column = 0;
                    break;
                }
            }
            if (!same_column)
                break;
            x_start++;
        }
        while (x_end > x_start) {
            int same_column = 1;
            for (y = y_start; y <= y_end; y++) {
                if (prv_srcp[y*prv_src_linesize + x_end] != cur_srcp[y*cur_src_linesize + x_end]) {
                    same_column = 0;
                    break;
                }
            }
            if (!same_column)
                break;
            x_end--;
        }
        width = x_end + 1 - x_start;

        if (x_start) {
            for (y = y_start; y <= y_end; y++)
                memcpy(cur_dstp + y*cur_dst_linesize,
                       prv_dstp + y*prv_dst_linesize, x_start);
        }
        if (x_end != cur_src->width - 1) {
            const int copy_len = cur_src->width - 1 - x_end;
            for (y = y_start; y <= y_end; y++)
                memcpy(cur_dstp + y*cur_dst_linesize + x_end + 1,
                       prv_dstp + y*prv_dst_linesize + x_end + 1,
                       copy_len);
        }
    }
    *xp = x_start;
    *yp = y_start;
    *wp = width;
    *hp = height;
}

static int apply_palette(AVFilterLink *inlink, AVFrame *in, AVFrame **outf)
{
    int x, y, w, h, ret;
    AVFilterContext *ctx = inlink->dst;
    PaletteUseContext *s = ctx->priv;
    AVFilterLink *outlink = inlink->dst->outputs[0];

    AVFrame *out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        *outf = NULL;
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

    set_processing_window(s->diff_mode, s->last_in, in,
                          s->last_out, out, &x, &y, &w, &h);
    av_frame_unref(s->last_in);
    av_frame_unref(s->last_out);
    if ((ret = av_frame_ref(s->last_in, in))       < 0 ||
        (ret = av_frame_ref(s->last_out, out))     < 0 ||
        (ret = ff_inlink_make_frame_writable(inlink, &s->last_in)) < 0) {
        av_frame_free(&out);
        *outf = NULL;
        return ret;
    }

    ff_dlog(ctx, "%dx%d rect: (%d;%d) -> (%d,%d) [area:%dx%d]\n",
            w, h, x, y, x+w, y+h, in->width, in->height);

    ret = s->set_frame(s, out, in, x, y, w, h);
    if (ret < 0) {
        av_frame_free(&out);
        *outf = NULL;
        return ret;
    }
    memcpy(out->data[1], s->palette, AVPALETTE_SIZE);
    *outf = out;
    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    int ret;
    AVFilterContext *ctx = outlink->src;
    PaletteUseContext *s = ctx->priv;

    ret = ff_framesync_init_dualinput(&s->fs, ctx);
    if (ret < 0)
        return ret;
    s->fs.opt_repeatlast = 1; // only 1 frame in the palette
    s->fs.in[1].before = s->fs.in[1].after = EXT_INFINITY;
    s->fs.on_event = load_apply_palette;

    outlink->w = ctx->inputs[0]->w;
    outlink->h = ctx->inputs[0]->h;

    outlink->time_base = ctx->inputs[0]->time_base;
    if ((ret = ff_framesync_configure(&s->fs)) < 0)
        return ret;
    return 0;
}

static int config_input_palette(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;

    if (inlink->w * inlink->h != AVPALETTE_COUNT) {
        av_log(ctx, AV_LOG_ERROR,
               "Palette input must contain exactly %d pixels. "
               "Specified input has %dx%d=%d pixels\n",
               AVPALETTE_COUNT, inlink->w, inlink->h,
               inlink->w * inlink->h);
        return AVERROR(EINVAL);
    }
    return 0;
}

static void load_palette(PaletteUseContext *s, const AVFrame *palette_frame)
{
    int i, x, y;
    const uint32_t *p = (const uint32_t *)palette_frame->data[0];
    const int p_linesize = palette_frame->linesize[0] >> 2;

    s->transparency_index = -1;

    if (s->new) {
        memset(s->palette, 0, sizeof(s->palette));
        memset(s->map, 0, sizeof(s->map));
        for (i = 0; i < CACHE_SIZE; i++)
            av_freep(&s->cache[i].entries);
        memset(s->cache, 0, sizeof(s->cache));
    }

    i = 0;
    for (y = 0; y < palette_frame->height; y++) {
        for (x = 0; x < palette_frame->width; x++) {
            s->palette[i] = p[x];
            if (p[x]>>24 < s->trans_thresh) {
                s->transparency_index = i; // we are assuming at most one transparent color in palette
            }
            i++;
        }
        p += p_linesize;
    }

    load_colormap(s);

    if (!s->new)
        s->palette_loaded = 1;
}

static int load_apply_palette(FFFrameSync *fs)
{
    AVFilterContext *ctx = fs->parent;
    AVFilterLink *inlink = ctx->inputs[0];
    PaletteUseContext *s = ctx->priv;
    AVFrame *master, *second, *out = NULL;
    int ret;

    // writable for error diffusal dithering
    ret = ff_framesync_dualinput_get_writable(fs, &master, &second);
    if (ret < 0)
        return ret;
    if (!master || !second) {
        av_frame_free(&master);
        return AVERROR_BUG;
    }
    if (!s->palette_loaded) {
        load_palette(s, second);
    }
    ret = apply_palette(inlink, master, &out);
    av_frame_free(&master);
    if (ret < 0)
        return ret;
    return ff_filter_frame(ctx->outputs[0], out);
}

#define DEFINE_SET_FRAME(name, value)                                           \
static int set_frame_##name(PaletteUseContext *s, AVFrame *out, AVFrame *in,    \
                            int x_start, int y_start, int w, int h)             \
{                                                                               \
    return set_frame(s, out, in, x_start, y_start, w, h, value);                \
}

DEFINE_SET_FRAME(none,            DITHERING_NONE)
DEFINE_SET_FRAME(bayer,           DITHERING_BAYER)
DEFINE_SET_FRAME(heckbert,        DITHERING_HECKBERT)
DEFINE_SET_FRAME(floyd_steinberg, DITHERING_FLOYD_STEINBERG)
DEFINE_SET_FRAME(sierra2,         DITHERING_SIERRA2)
DEFINE_SET_FRAME(sierra2_4a,      DITHERING_SIERRA2_4A)
DEFINE_SET_FRAME(sierra3,         DITHERING_SIERRA3)
DEFINE_SET_FRAME(burkes,          DITHERING_BURKES)
DEFINE_SET_FRAME(atkinson,        DITHERING_ATKINSON)

static const set_frame_func set_frame_lut[NB_DITHERING] = {
    [DITHERING_NONE]            = set_frame_none,
    [DITHERING_BAYER]           = set_frame_bayer,
    [DITHERING_HECKBERT]        = set_frame_heckbert,
    [DITHERING_FLOYD_STEINBERG] = set_frame_floyd_steinberg,
    [DITHERING_SIERRA2]         = set_frame_sierra2,
    [DITHERING_SIERRA2_4A]      = set_frame_sierra2_4a,
    [DITHERING_SIERRA3]         = set_frame_sierra3,
    [DITHERING_BURKES]          = set_frame_burkes,
    [DITHERING_ATKINSON]        = set_frame_atkinson,
};

static int dither_value(int p)
{
    const int q = p ^ (p >> 3);
    return   (p & 4) >> 2 | (q & 4) >> 1 \
           | (p & 2) << 1 | (q & 2) << 2 \
           | (p & 1) << 4 | (q & 1) << 5;
}

static av_cold int init(AVFilterContext *ctx)
{
    PaletteUseContext *s = ctx->priv;

    s->last_in  = av_frame_alloc();
    s->last_out = av_frame_alloc();
    if (!s->last_in || !s->last_out)
        return AVERROR(ENOMEM);

    s->set_frame = set_frame_lut[s->dither];

    if (s->dither == DITHERING_BAYER) {
        const int delta = 1 << (5 - s->bayer_scale); // to avoid too much luma

        for (int i = 0; i < FF_ARRAY_ELEMS(s->ordered_dither); i++)
            s->ordered_dither[i] = (dither_value(i) >> s->bayer_scale) - delta;
    }

    return 0;
}

static int activate(AVFilterContext *ctx)
{
    PaletteUseContext *s = ctx->priv;
    return ff_framesync_activate(&s->fs);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    PaletteUseContext *s = ctx->priv;

    ff_framesync_uninit(&s->fs);
    for (int i = 0; i < CACHE_SIZE; i++)
        av_freep(&s->cache[i].entries);
    av_frame_free(&s->last_in);
    av_frame_free(&s->last_out);
}

static const AVFilterPad paletteuse_inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_VIDEO,
    },{
        .name           = "palette",
        .type           = AVMEDIA_TYPE_VIDEO,
        .config_props   = config_input_palette,
    },
};

static const AVFilterPad paletteuse_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
};

const AVFilter ff_vf_paletteuse = {
    .name          = "paletteuse",
    .description   = NULL_IF_CONFIG_SMALL("Use a palette to downsample an input video stream."),
    .priv_size     = sizeof(PaletteUseContext),
    .init          = init,
    .uninit        = uninit,
    .activate      = activate,
    FILTER_INPUTS(paletteuse_inputs),
    FILTER_OUTPUTS(paletteuse_outputs),
    FILTER_QUERY_FUNC(query_formats),
    .priv_class    = &paletteuse_class,
};
