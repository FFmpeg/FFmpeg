/*
 * Copyright (c) 2015 Stupeflix
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
#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "libavutil/qsort.h"
#include "avfilter.h"
#include "framesync.h"
#include "internal.h"

enum dithering_mode {
    DITHERING_NONE,
    DITHERING_BAYER,
    DITHERING_HECKBERT,
    DITHERING_FLOYD_STEINBERG,
    DITHERING_SIERRA2,
    DITHERING_SIERRA2_4A,
    NB_DITHERING
};

enum color_search_method {
    COLOR_SEARCH_NNS_ITERATIVE,
    COLOR_SEARCH_NNS_RECURSIVE,
    COLOR_SEARCH_BRUTEFORCE,
    NB_COLOR_SEARCHES
};

enum diff_mode {
    DIFF_MODE_NONE,
    DIFF_MODE_RECTANGLE,
    NB_DIFF_MODE
};

struct color_node {
    uint8_t val[4];
    uint8_t palette_id;
    int split;
    int left_id, right_id;
};

#define NBITS 5
#define CACHE_SIZE (1<<(4*NBITS))

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
    int use_alpha;
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
    int color_search_method;
    int calc_mean_err;
    uint64_t total_mean_err;
    int debug_accuracy;
} PaletteUseContext;

#define OFFSET(x) offsetof(PaletteUseContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)
static const AVOption paletteuse_options[] = {
    { "dither", "select dithering mode", OFFSET(dither), AV_OPT_TYPE_INT, {.i64=DITHERING_SIERRA2_4A}, 0, NB_DITHERING-1, FLAGS, "dithering_mode" },
        { "bayer",           "ordered 8x8 bayer dithering (deterministic)",                            0, AV_OPT_TYPE_CONST, {.i64=DITHERING_BAYER},           INT_MIN, INT_MAX, FLAGS, "dithering_mode" },
        { "heckbert",        "dithering as defined by Paul Heckbert in 1982 (simple error diffusion)", 0, AV_OPT_TYPE_CONST, {.i64=DITHERING_HECKBERT},        INT_MIN, INT_MAX, FLAGS, "dithering_mode" },
        { "floyd_steinberg", "Floyd and Steingberg dithering (error diffusion)",                       0, AV_OPT_TYPE_CONST, {.i64=DITHERING_FLOYD_STEINBERG}, INT_MIN, INT_MAX, FLAGS, "dithering_mode" },
        { "sierra2",         "Frankie Sierra dithering v2 (error diffusion)",                          0, AV_OPT_TYPE_CONST, {.i64=DITHERING_SIERRA2},         INT_MIN, INT_MAX, FLAGS, "dithering_mode" },
        { "sierra2_4a",      "Frankie Sierra dithering v2 \"Lite\" (error diffusion)",                 0, AV_OPT_TYPE_CONST, {.i64=DITHERING_SIERRA2_4A},      INT_MIN, INT_MAX, FLAGS, "dithering_mode" },
    { "bayer_scale", "set scale for bayer dithering", OFFSET(bayer_scale), AV_OPT_TYPE_INT, {.i64=2}, 0, 5, FLAGS },
    { "diff_mode",   "set frame difference mode",     OFFSET(diff_mode),   AV_OPT_TYPE_INT, {.i64=DIFF_MODE_NONE}, 0, NB_DIFF_MODE-1, FLAGS, "diff_mode" },
        { "rectangle", "process smallest different rectangle", 0, AV_OPT_TYPE_CONST, {.i64=DIFF_MODE_RECTANGLE}, INT_MIN, INT_MAX, FLAGS, "diff_mode" },
    { "new", "take new palette for each output frame", OFFSET(new), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS },
    { "alpha_threshold", "set the alpha threshold for transparency", OFFSET(trans_thresh), AV_OPT_TYPE_INT, {.i64=128}, 0, 255, FLAGS },
    { "use_alpha", "use alpha channel for mapping", OFFSET(use_alpha), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS },

    /* following are the debug options, not part of the official API */
    { "debug_kdtree", "save Graphviz graph of the kdtree in specified file", OFFSET(dot_filename), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    { "color_search", "set reverse colormap color search method", OFFSET(color_search_method), AV_OPT_TYPE_INT, {.i64=COLOR_SEARCH_NNS_ITERATIVE}, 0, NB_COLOR_SEARCHES-1, FLAGS, "search" },
        { "nns_iterative", "iterative search",             0, AV_OPT_TYPE_CONST, {.i64=COLOR_SEARCH_NNS_ITERATIVE}, INT_MIN, INT_MAX, FLAGS, "search" },
        { "nns_recursive", "recursive search",             0, AV_OPT_TYPE_CONST, {.i64=COLOR_SEARCH_NNS_RECURSIVE}, INT_MIN, INT_MAX, FLAGS, "search" },
        { "bruteforce",    "brute-force into the palette", 0, AV_OPT_TYPE_CONST, {.i64=COLOR_SEARCH_BRUTEFORCE},    INT_MIN, INT_MAX, FLAGS, "search" },
    { "mean_err", "compute and print mean error", OFFSET(calc_mean_err), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS },
    { "debug_accuracy", "test color search accuracy", OFFSET(debug_accuracy), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS },
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
    return                px >> 24                                        << 24
         | av_clip_uint8((px >> 16 & 0xff) + ((er * scale) / (1<<shift))) << 16
         | av_clip_uint8((px >>  8 & 0xff) + ((eg * scale) / (1<<shift))) <<  8
         | av_clip_uint8((px       & 0xff) + ((eb * scale) / (1<<shift)));
}

static av_always_inline int diff(const uint8_t *c1, const uint8_t *c2, const PaletteUseContext *s)
{
    // XXX: try L*a*b with CIE76 (dL*dL + da*da + db*db)
    const int da = c1[0] - c2[0];
    const int dr = c1[1] - c2[1];
    const int dg = c1[2] - c2[2];
    const int db = c1[3] - c2[3];

    if (s->use_alpha)
        return da*da + dr*dr + dg*dg + db*db;

    if (c1[0] < s->trans_thresh && c2[0] < s->trans_thresh) {
        return 0;
    } else if (c1[0] >= s->trans_thresh && c2[0] >= s->trans_thresh) {
        return dr*dr + dg*dg + db*db;
    } else {
        return 255*255 + 255*255 + 255*255;
    }
}

static av_always_inline uint8_t colormap_nearest_bruteforce(const PaletteUseContext *s, const uint8_t *argb)
{
    int i, pal_id = -1, min_dist = INT_MAX;

    for (i = 0; i < AVPALETTE_COUNT; i++) {
        const uint32_t c = s->palette[i];

        if (s->use_alpha || c >> 24 >= s->trans_thresh) { // ignore transparent entry
            const uint8_t palargb[] = {
                s->palette[i]>>24 & 0xff,
                s->palette[i]>>16 & 0xff,
                s->palette[i]>> 8 & 0xff,
                s->palette[i]     & 0xff,
            };
            const int d = diff(palargb, argb, s);
            if (d < min_dist) {
                pal_id = i;
                min_dist = d;
            }
        }
    }
    return pal_id;
}

/* Recursive form, simpler but a bit slower. Kept for reference. */
struct nearest_color {
    int node_pos;
    int dist_sqd;
};

static void colormap_nearest_node(const PaletteUseContext *s,
                                  const struct color_node *map,
                                  const int node_pos,
                                  const uint8_t *target,
                                  struct nearest_color *nearest)
{
    const struct color_node *kd = map + node_pos;
    const int split = kd->split;
    int dx, nearer_kd_id, further_kd_id;
    const uint8_t *current = kd->val;
    const int current_to_target = diff(target, current, s);

    if (current_to_target < nearest->dist_sqd) {
        nearest->node_pos = node_pos;
        nearest->dist_sqd = current_to_target;
    }

    if (kd->left_id != -1 || kd->right_id != -1) {
        dx = target[split] - current[split];

        if (dx <= 0) nearer_kd_id = kd->left_id,  further_kd_id = kd->right_id;
        else         nearer_kd_id = kd->right_id, further_kd_id = kd->left_id;

        if (nearer_kd_id != -1)
            colormap_nearest_node(s, map, nearer_kd_id, target, nearest);

        if (further_kd_id != -1 && dx*dx < nearest->dist_sqd)
            colormap_nearest_node(s, map, further_kd_id, target, nearest);
    }
}

static av_always_inline uint8_t colormap_nearest_recursive(const PaletteUseContext *s, const struct color_node *node, const uint8_t *rgb)
{
    struct nearest_color res = {.dist_sqd = INT_MAX, .node_pos = -1};
    colormap_nearest_node(s, node, 0, rgb, &res);
    return node[res.node_pos].palette_id;
}

struct stack_node {
    int color_id;
    int dx2;
};

static av_always_inline uint8_t colormap_nearest_iterative(const PaletteUseContext *s, const struct color_node *root, const uint8_t *target)
{
    int pos = 0, best_node_id = -1, best_dist = INT_MAX, cur_color_id = 0;
    struct stack_node nodes[16];
    struct stack_node *node = &nodes[0];

    for (;;) {

        const struct color_node *kd = &root[cur_color_id];
        const uint8_t *current = kd->val;
        const int current_to_target = diff(target, current, s);

        /* Compare current color node to the target and update our best node if
         * it's actually better. */
        if (current_to_target < best_dist) {
            best_node_id = cur_color_id;
            if (!current_to_target)
                goto end; // exact match, we can return immediately
            best_dist = current_to_target;
        }

        /* Check if it's not a leaf */
        if (kd->left_id != -1 || kd->right_id != -1) {
            const int split = kd->split;
            const int dx = target[split] - current[split];
            int nearer_kd_id, further_kd_id;

            /* Define which side is the most interesting. */
            if (dx <= 0) nearer_kd_id = kd->left_id,  further_kd_id = kd->right_id;
            else         nearer_kd_id = kd->right_id, further_kd_id = kd->left_id;

            if (nearer_kd_id != -1) {
                if (further_kd_id != -1) {
                    /* Here, both paths are defined, so we push a state for
                     * when we are going back. */
                    node->color_id = further_kd_id;
                    node->dx2 = dx*dx;
                    pos++;
                    node++;
                }
                /* We can now update current color with the most probable path
                 * (no need to create a state since there is nothing to save
                 * anymore). */
                cur_color_id = nearer_kd_id;
                continue;
            } else if (dx*dx < best_dist) {
                /* The nearest path isn't available, so there is only one path
                 * possible and it's the least probable. We enter it only if the
                 * distance from the current point to the hyper rectangle is
                 * less than our best distance. */
                cur_color_id = further_kd_id;
                continue;
            }
        }

        /* Unstack as much as we can, typically as long as the least probable
         * branch aren't actually probable. */
        do {
            if (--pos < 0)
                goto end;
            node--;
        } while (node->dx2 >= best_dist);

        /* We got a node where the least probable branch might actually contain
         * a relevant color. */
        cur_color_id = node->color_id;
    }

end:
    return root[best_node_id].palette_id;
}

#define COLORMAP_NEAREST(s, search, root, target)                                    \
    search == COLOR_SEARCH_NNS_ITERATIVE ? colormap_nearest_iterative(s, root, target) :      \
    search == COLOR_SEARCH_NNS_RECURSIVE ? colormap_nearest_recursive(s, root, target) :      \
                                           colormap_nearest_bruteforce(s, target)

/**
 * Check if the requested color is in the cache already. If not, find it in the
 * color tree and cache it.
 * Note: a, r, g, and b are the components of color, but are passed as well to avoid
 * recomputing them (they are generally computed by the caller for other uses).
 */
static av_always_inline int color_get(PaletteUseContext *s, uint32_t color,
                                      uint8_t a, uint8_t r, uint8_t g, uint8_t b,
                                      const enum color_search_method search_method)
{
    int i;
    const uint8_t argb_elts[] = {a, r, g, b};
    const uint8_t rhash = r & ((1<<NBITS)-1);
    const uint8_t ghash = g & ((1<<NBITS)-1);
    const uint8_t bhash = b & ((1<<NBITS)-1);
    const unsigned hash = rhash<<(NBITS*2) | ghash<<NBITS | bhash;
    struct cache_node *node = &s->cache[hash];
    struct cached_color *e;

    // first, check for transparency
    if (a < s->trans_thresh && s->transparency_index >= 0) {
        return s->transparency_index;
    }

    for (i = 0; i < node->nb_entries; i++) {
        e = &node->entries[i];
        if (e->color == color)
            return e->pal_entry;
    }

    e = av_dynarray2_add((void**)&node->entries, &node->nb_entries,
                         sizeof(*node->entries), NULL);
    if (!e)
        return AVERROR(ENOMEM);
    e->color = color;
    e->pal_entry = COLORMAP_NEAREST(s, search_method, s->map, argb_elts);

    return e->pal_entry;
}

static av_always_inline int get_dst_color_err(PaletteUseContext *s,
                                              uint32_t c, int *ea, int *er, int *eg, int *eb,
                                              const enum color_search_method search_method)
{
    const uint8_t a = c >> 24 & 0xff;
    const uint8_t r = c >> 16 & 0xff;
    const uint8_t g = c >>  8 & 0xff;
    const uint8_t b = c       & 0xff;
    uint32_t dstc;
    const int dstx = color_get(s, c, a, r, g, b, search_method);
    if (dstx < 0)
        return dstx;
    dstc = s->palette[dstx];
    if (dstx == s->transparency_index) {
        *ea =*er = *eg = *eb = 0;
    } else {
        *ea = (int)a - (int)(dstc >> 24 & 0xff);
        *er = (int)r - (int)(dstc >> 16 & 0xff);
        *eg = (int)g - (int)(dstc >>  8 & 0xff);
        *eb = (int)b - (int)(dstc       & 0xff);
    }
    return dstx;
}

static av_always_inline int set_frame(PaletteUseContext *s, AVFrame *out, AVFrame *in,
                                      int x_start, int y_start, int w, int h,
                                      enum dithering_mode dither,
                                      const enum color_search_method search_method)
{
    int x, y;
    const int src_linesize = in ->linesize[0] >> 2;
    const int dst_linesize = out->linesize[0];
    uint32_t *src = ((uint32_t *)in ->data[0]) + y_start*src_linesize;
    uint8_t  *dst =              out->data[0]  + y_start*dst_linesize;

    w += x_start;
    h += y_start;

    for (y = y_start; y < h; y++) {
        for (x = x_start; x < w; x++) {
            int ea, er, eg, eb;

            if (dither == DITHERING_BAYER) {
                const int d = s->ordered_dither[(y & 7)<<3 | (x & 7)];
                const uint8_t a8 = src[x] >> 24 & 0xff;
                const uint8_t r8 = src[x] >> 16 & 0xff;
                const uint8_t g8 = src[x] >>  8 & 0xff;
                const uint8_t b8 = src[x]       & 0xff;
                const uint8_t r = av_clip_uint8(r8 + d);
                const uint8_t g = av_clip_uint8(g8 + d);
                const uint8_t b = av_clip_uint8(b8 + d);
                const uint32_t color_new = (unsigned)(a8) << 24 | r << 16 | g << 8 | b;
                const int color = color_get(s, color_new, a8, r, g, b, search_method);

                if (color < 0)
                    return color;
                dst[x] = color;

            } else if (dither == DITHERING_HECKBERT) {
                const int right = x < w - 1, down = y < h - 1;
                const int color = get_dst_color_err(s, src[x], &ea, &er, &eg, &eb, search_method);

                if (color < 0)
                    return color;
                dst[x] = color;

                if (right)         src[               x + 1] = dither_color(src[               x + 1], er, eg, eb, 3, 3);
                if (         down) src[src_linesize + x    ] = dither_color(src[src_linesize + x    ], er, eg, eb, 3, 3);
                if (right && down) src[src_linesize + x + 1] = dither_color(src[src_linesize + x + 1], er, eg, eb, 2, 3);

            } else if (dither == DITHERING_FLOYD_STEINBERG) {
                const int right = x < w - 1, down = y < h - 1, left = x > x_start;
                const int color = get_dst_color_err(s, src[x], &ea, &er, &eg, &eb, search_method);

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
                const int color = get_dst_color_err(s, src[x], &ea, &er, &eg, &eb, search_method);

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
                const int color = get_dst_color_err(s, src[x], &ea, &er, &eg, &eb, search_method);

                if (color < 0)
                    return color;
                dst[x] = color;

                if (right)         src[               x + 1] = dither_color(src[               x + 1], er, eg, eb, 2, 2);
                if (left  && down) src[src_linesize + x - 1] = dither_color(src[src_linesize + x - 1], er, eg, eb, 1, 2);
                if (         down) src[src_linesize + x    ] = dither_color(src[src_linesize + x    ], er, eg, eb, 1, 2);

            } else {
                const uint8_t a = src[x] >> 24 & 0xff;
                const uint8_t r = src[x] >> 16 & 0xff;
                const uint8_t g = src[x] >>  8 & 0xff;
                const uint8_t b = src[x]       & 0xff;
                const int color = color_get(s, src[x], a, r, g, b, search_method);

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
    const uint32_t fontcolor = node->val[1] > 0x50 &&
                               node->val[2] > 0x50 &&
                               node->val[3] > 0x50 ? 0 : 0xffffff;
    const int rgb_comp = node->split - 1;
    av_bprintf(buf, "%*cnode%d ["
               "label=\"%c%02X%c%02X%c%02X%c\" "
               "fillcolor=\"#%02x%02x%02x\" "
               "fontcolor=\"#%06"PRIX32"\"]\n",
               depth*INDENT, ' ', node->palette_id,
               "[  "[rgb_comp], node->val[1],
               "][ "[rgb_comp], node->val[2],
               " ]["[rgb_comp], node->val[3],
               "  ]"[rgb_comp],
               node->val[1], node->val[2], node->val[3],
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

static int debug_accuracy(const PaletteUseContext *s)
{
    int r, g, b, ret = 0;

    for (r = 0; r < 256; r++) {
        for (g = 0; g < 256; g++) {
            for (b = 0; b < 256; b++) {
                const uint8_t argb[] = {0xff, r, g, b};
                const int r1 = COLORMAP_NEAREST(s, s->color_search_method, s->map, argb);
                const int r2 = colormap_nearest_bruteforce(s, argb);
                if (r1 != r2) {
                    const uint32_t c1 = s->palette[r1];
                    const uint32_t c2 = s->palette[r2];
                    const uint8_t a1 = s->use_alpha ? c1>>24 & 0xff : 0xff;
                    const uint8_t a2 = s->use_alpha ? c2>>24 & 0xff : 0xff;
                    const uint8_t palargb1[] = { a1, c1>>16 & 0xff, c1>> 8 & 0xff, c1 & 0xff };
                    const uint8_t palargb2[] = { a2, c2>>16 & 0xff, c2>> 8 & 0xff, c2 & 0xff };
                    const int d1 = diff(palargb1, argb, s);
                    const int d2 = diff(palargb2, argb, s);
                    if (d1 != d2) {
                        if (s->use_alpha)
                            av_log(NULL, AV_LOG_ERROR,
                                   "/!\\ %02X%02X%02X: %d ! %d (%08"PRIX32" ! %08"PRIX32") / dist: %d ! %d\n",
                                   r, g, b, r1, r2, c1, c2, d1, d2);
                        else
                            av_log(NULL, AV_LOG_ERROR,
                                   "/!\\ %02X%02X%02X: %d ! %d (%06"PRIX32" ! %06"PRIX32") / dist: %d ! %d\n",
                                   r, g, b, r1, r2, c1 & 0xffffff, c2 & 0xffffff, d1, d2);
                        ret = 1;
                    }
                }
            }
        }
    }
    return ret;
}

struct color {
    uint32_t value;
    uint8_t pal_id;
};

struct color_rect {
    uint8_t min[4];
    uint8_t max[4];
};

typedef int (*cmp_func)(const void *, const void *);

#define DECLARE_CMP_FUNC(name, pos)                     \
static int cmp_##name(const void *pa, const void *pb)   \
{                                                       \
    const struct color *a = pa;                         \
    const struct color *b = pb;                         \
    return   (int)(a->value >> (8 * (3 - (pos))) & 0xff)     \
           - (int)(b->value >> (8 * (3 - (pos))) & 0xff);    \
}

DECLARE_CMP_FUNC(a, 0)
DECLARE_CMP_FUNC(r, 1)
DECLARE_CMP_FUNC(g, 2)
DECLARE_CMP_FUNC(b, 3)

static const cmp_func cmp_funcs[] = {cmp_a, cmp_r, cmp_g, cmp_b};

static int get_next_color(const uint8_t *color_used, const PaletteUseContext *s,
                          int *component, const struct color_rect *box)
{
    int wa, wr, wg, wb;
    int i, longest = 0;
    unsigned nb_color = 0;
    struct color_rect ranges;
    struct color tmp_pal[256];
    cmp_func cmpf;

    ranges.min[0] = ranges.min[1] = ranges.min[2]  = ranges.min[3]= 0xff;
    ranges.max[0] = ranges.max[1] = ranges.max[2]  = ranges.max[3]= 0x00;

    for (i = 0; i < AVPALETTE_COUNT; i++) {
        const uint32_t c = s->palette[i];
        const uint8_t a = c >> 24 & 0xff;
        const uint8_t r = c >> 16 & 0xff;
        const uint8_t g = c >>  8 & 0xff;
        const uint8_t b = c       & 0xff;

        if (!s->use_alpha && a < s->trans_thresh) {
            continue;
        }

        if (color_used[i] || (a != 0xff && !s->use_alpha) ||
            r < box->min[1] || g < box->min[2] || b < box->min[3] ||
            r > box->max[1] || g > box->max[2] || b > box->max[3])
            continue;

        if (s->use_alpha && (a < box->min[0] || a > box->max[0]))
            continue;

        if (a < ranges.min[0]) ranges.min[0] = a;
        if (r < ranges.min[1]) ranges.min[1] = r;
        if (g < ranges.min[2]) ranges.min[2] = g;
        if (b < ranges.min[3]) ranges.min[3] = b;

        if (a > ranges.max[0]) ranges.max[0] = a;
        if (r > ranges.max[1]) ranges.max[1] = r;
        if (g > ranges.max[2]) ranges.max[2] = g;
        if (b > ranges.max[3]) ranges.max[3] = b;

        tmp_pal[nb_color].value  = c;
        tmp_pal[nb_color].pal_id = i;

        nb_color++;
    }

    if (!nb_color)
        return -1;

    /* define longest axis that will be the split component */
    wa = ranges.max[0] - ranges.min[0];
    wr = ranges.max[1] - ranges.min[1];
    wg = ranges.max[2] - ranges.min[2];
    wb = ranges.max[3] - ranges.min[3];

    if (s->use_alpha) {
        if (wa >= wr && wa >= wb && wa >= wg) longest = 0;
        if (wr >= wg && wr >= wb && wr >= wa) longest = 1;
        if (wg >= wr && wg >= wb && wg >= wa) longest = 2;
        if (wb >= wr && wb >= wg && wb >= wa) longest = 3;
    } else {
        if (wr >= wg && wr >= wb) longest = 1;
        if (wg >= wr && wg >= wb) longest = 2;
        if (wb >= wr && wb >= wg) longest = 3;
    }

    cmpf = cmp_funcs[longest];
    *component = longest;

    /* sort along this axis to get median */
    AV_QSORT(tmp_pal, nb_color, struct color, cmpf);

    return tmp_pal[nb_color >> 1].pal_id;
}

static int colormap_insert(struct color_node *map,
                           uint8_t *color_used,
                           int *nb_used,
                           const PaletteUseContext *s,
                           const struct color_rect *box)
{
    uint32_t c;
    int component, cur_id;
    int node_left_id = -1, node_right_id = -1;
    struct color_node *node;
    struct color_rect box1, box2;
    const int pal_id = get_next_color(color_used, s, &component, box);

    if (pal_id < 0)
        return -1;

    /* create new node with that color */
    cur_id = (*nb_used)++;
    c = s->palette[pal_id];
    node = &map[cur_id];
    node->split = component;
    node->palette_id = pal_id;
    node->val[0] = c>>24 & 0xff;
    node->val[1] = c>>16 & 0xff;
    node->val[2] = c>> 8 & 0xff;
    node->val[3] = c     & 0xff;

    color_used[pal_id] = 1;

    /* get the two boxes this node creates */
    box1 = box2 = *box;
    box1.max[component] = node->val[component];
    box2.min[component] = FFMIN(node->val[component] + 1, 255);

    node_left_id = colormap_insert(map, color_used, nb_used, s, &box1);

    if (box2.min[component] <= box2.max[component])
        node_right_id = colormap_insert(map, color_used, nb_used, s, &box2);

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

static int cmp_pal_entry_alpha(const void *a, const void *b)
{
    const int c1 = *(const uint32_t *)a;
    const int c2 = *(const uint32_t *)b;
    return c1 - c2;
}

static void load_colormap(PaletteUseContext *s)
{
    int i, nb_used = 0;
    uint8_t color_used[AVPALETTE_COUNT] = {0};
    uint32_t last_color = 0;
    struct color_rect box;

    if (!s->use_alpha && s->transparency_index >= 0) {
        FFSWAP(uint32_t, s->palette[s->transparency_index], s->palette[255]);
    }

    /* disable transparent colors and dups */
    qsort(s->palette, AVPALETTE_COUNT-(s->transparency_index >= 0), sizeof(*s->palette),
        s->use_alpha ? cmp_pal_entry_alpha : cmp_pal_entry);

    for (i = 0; i < AVPALETTE_COUNT; i++) {
        const uint32_t c = s->palette[i];
        if (i != 0 && c == last_color) {
            color_used[i] = 1;
            continue;
        }
        last_color = c;
        if (!s->use_alpha && c >> 24 < s->trans_thresh) {
            color_used[i] = 1; // ignore transparent color(s)
            continue;
        }
    }

    box.min[0] = box.min[1] = box.min[2] = box.min[3] = 0x00;
    box.max[0] = box.max[1] = box.max[2] = box.max[3] = 0xff;

    colormap_insert(s->map, color_used, &nb_used, s, &box);

    if (s->dot_filename)
        disp_tree(s->map, s->dot_filename);

    if (s->debug_accuracy) {
        if (!debug_accuracy(s))
            av_log(NULL, AV_LOG_INFO, "Accuracy check passed\n");
    }
}

static void debug_mean_error(PaletteUseContext *s, const AVFrame *in1,
                             const AVFrame *in2, int frame_count)
{
    int x, y;
    const uint32_t *palette = s->palette;
    uint32_t *src1 = (uint32_t *)in1->data[0];
    uint8_t  *src2 =             in2->data[0];
    const int src1_linesize = in1->linesize[0] >> 2;
    const int src2_linesize = in2->linesize[0];
    const float div = in1->width * in1->height * (s->use_alpha ? 4 : 3);
    unsigned mean_err = 0;

    for (y = 0; y < in1->height; y++) {
        for (x = 0; x < in1->width; x++) {
            const uint32_t c1 = src1[x];
            const uint32_t c2 = palette[src2[x]];
            const uint8_t a1 = s->use_alpha ? c1>>24 & 0xff : 0xff;
            const uint8_t a2 = s->use_alpha ? c2>>24 & 0xff : 0xff;
            const uint8_t argb1[] = {a1, c1 >> 16 & 0xff, c1 >> 8 & 0xff, c1 & 0xff};
            const uint8_t argb2[] = {a2, c2 >> 16 & 0xff, c2 >> 8 & 0xff, c2 & 0xff};
            mean_err += diff(argb1, argb2, s);
        }
        src1 += src1_linesize;
        src2 += src2_linesize;
    }

    s->total_mean_err += mean_err;

    av_log(NULL, AV_LOG_INFO, "MEP:%.3f TotalMEP:%.3f\n",
           mean_err / div, s->total_mean_err / (div * frame_count));
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
        (ret = av_frame_make_writable(s->last_in)) < 0) {
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
    if (s->calc_mean_err)
        debug_mean_error(s, in, out, inlink->frame_count_out);
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
            if (!s->use_alpha && p[x]>>24 < s->trans_thresh) {
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

#define DEFINE_SET_FRAME(color_search, name, value)                             \
static int set_frame_##name(PaletteUseContext *s, AVFrame *out, AVFrame *in,    \
                            int x_start, int y_start, int w, int h)             \
{                                                                               \
    return set_frame(s, out, in, x_start, y_start, w, h, value, color_search);  \
}

#define DEFINE_SET_FRAME_COLOR_SEARCH(color_search, color_search_macro)                                 \
    DEFINE_SET_FRAME(color_search_macro, color_search##_##none,            DITHERING_NONE)              \
    DEFINE_SET_FRAME(color_search_macro, color_search##_##bayer,           DITHERING_BAYER)             \
    DEFINE_SET_FRAME(color_search_macro, color_search##_##heckbert,        DITHERING_HECKBERT)          \
    DEFINE_SET_FRAME(color_search_macro, color_search##_##floyd_steinberg, DITHERING_FLOYD_STEINBERG)   \
    DEFINE_SET_FRAME(color_search_macro, color_search##_##sierra2,         DITHERING_SIERRA2)           \
    DEFINE_SET_FRAME(color_search_macro, color_search##_##sierra2_4a,      DITHERING_SIERRA2_4A)        \

DEFINE_SET_FRAME_COLOR_SEARCH(nns_iterative, COLOR_SEARCH_NNS_ITERATIVE)
DEFINE_SET_FRAME_COLOR_SEARCH(nns_recursive, COLOR_SEARCH_NNS_RECURSIVE)
DEFINE_SET_FRAME_COLOR_SEARCH(bruteforce,    COLOR_SEARCH_BRUTEFORCE)

#define DITHERING_ENTRIES(color_search) {       \
    set_frame_##color_search##_none,            \
    set_frame_##color_search##_bayer,           \
    set_frame_##color_search##_heckbert,        \
    set_frame_##color_search##_floyd_steinberg, \
    set_frame_##color_search##_sierra2,         \
    set_frame_##color_search##_sierra2_4a,      \
}

static const set_frame_func set_frame_lut[NB_COLOR_SEARCHES][NB_DITHERING] = {
    DITHERING_ENTRIES(nns_iterative),
    DITHERING_ENTRIES(nns_recursive),
    DITHERING_ENTRIES(bruteforce),
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

    s->set_frame = set_frame_lut[s->color_search_method][s->dither];

    if (s->dither == DITHERING_BAYER) {
        int i;
        const int delta = 1 << (5 - s->bayer_scale); // to avoid too much luma

        for (i = 0; i < FF_ARRAY_ELEMS(s->ordered_dither); i++)
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
    int i;
    PaletteUseContext *s = ctx->priv;

    ff_framesync_uninit(&s->fs);
    for (i = 0; i < CACHE_SIZE; i++)
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
