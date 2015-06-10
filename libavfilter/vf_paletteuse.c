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
#include "libavutil/opt.h"
#include "libavutil/qsort.h"
#include "dualinput.h"
#include "avfilter.h"

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
    uint8_t val[3];
    uint8_t palette_id;
    int split;
    int left_id, right_id;
};

#define NBITS 5
#define CACHE_SIZE (1<<(3*NBITS))

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
    FFDualInputContext dinput;
    struct cache_node cache[CACHE_SIZE];    /* lookup cache */
    struct color_node map[AVPALETTE_COUNT]; /* 3D-Tree (KD-Tree with K=3) for reverse colormap */
    uint32_t palette[AVPALETTE_COUNT];
    int palette_loaded;
    int dither;
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
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
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

    /* following are the debug options, not part of the official API */
    { "debug_kdtree", "save Graphviz graph of the kdtree in specified file", OFFSET(dot_filename), AV_OPT_TYPE_STRING, {.str=NULL}, CHAR_MIN, CHAR_MAX, FLAGS },
    { "color_search", "set reverse colormap color search method", OFFSET(color_search_method), AV_OPT_TYPE_INT, {.i64=COLOR_SEARCH_NNS_ITERATIVE}, 0, NB_COLOR_SEARCHES-1, FLAGS, "search" },
        { "nns_iterative", "iterative search",             0, AV_OPT_TYPE_CONST, {.i64=COLOR_SEARCH_NNS_ITERATIVE}, INT_MIN, INT_MAX, FLAGS, "search" },
        { "nns_recursive", "recursive search",             0, AV_OPT_TYPE_CONST, {.i64=COLOR_SEARCH_NNS_RECURSIVE}, INT_MIN, INT_MAX, FLAGS, "search" },
        { "bruteforce",    "brute-force into the palette", 0, AV_OPT_TYPE_CONST, {.i64=COLOR_SEARCH_BRUTEFORCE},    INT_MIN, INT_MAX, FLAGS, "search" },
    { "mean_err", "compute and print mean error", OFFSET(calc_mean_err), AV_OPT_TYPE_INT, {.i64=0}, 0, 1, FLAGS },
    { "debug_accuracy", "test color search accuracy", OFFSET(debug_accuracy), AV_OPT_TYPE_FLAGS, {.i64=0}, 0, 1, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(paletteuse);

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat in_fmts[]    = {AV_PIX_FMT_RGB32, AV_PIX_FMT_NONE};
    static const enum AVPixelFormat inpal_fmts[] = {AV_PIX_FMT_RGB32, AV_PIX_FMT_NONE};
    static const enum AVPixelFormat out_fmts[]   = {AV_PIX_FMT_PAL8,  AV_PIX_FMT_NONE};
    AVFilterFormats *in    = ff_make_format_list(in_fmts);
    AVFilterFormats *inpal = ff_make_format_list(inpal_fmts);
    AVFilterFormats *out   = ff_make_format_list(out_fmts);
    if (!in || !inpal || !out) {
        av_freep(&in);
        av_freep(&inpal);
        av_freep(&out);
        return AVERROR(ENOMEM);
    }
    ff_formats_ref(in,    &ctx->inputs[0]->out_formats);
    ff_formats_ref(inpal, &ctx->inputs[1]->out_formats);
    ff_formats_ref(out,   &ctx->outputs[0]->in_formats);
    return 0;
}

static av_always_inline int dither_color(uint32_t px, int er, int eg, int eb, int scale, int shift)
{
    return av_clip_uint8((px >> 16 & 0xff) + ((er * scale) / (1<<shift))) << 16
         | av_clip_uint8((px >>  8 & 0xff) + ((eg * scale) / (1<<shift))) <<  8
         | av_clip_uint8((px       & 0xff) + ((eb * scale) / (1<<shift)));
}

static av_always_inline int diff(const uint8_t *c1, const uint8_t *c2)
{
    // XXX: try L*a*b with CIE76 (dL*dL + da*da + db*db)
    const int dr = c1[0] - c2[0];
    const int dg = c1[1] - c2[1];
    const int db = c1[2] - c2[2];
    return dr*dr + dg*dg + db*db;
}

static av_always_inline uint8_t colormap_nearest_bruteforce(const uint32_t *palette, const uint8_t *rgb)
{
    int i, pal_id = -1, min_dist = INT_MAX;

    for (i = 0; i < AVPALETTE_COUNT; i++) {
        const uint32_t c = palette[i];

        if ((c & 0xff000000) == 0xff000000) { // ignore transparent entry
            const uint8_t palrgb[] = {
                palette[i]>>16 & 0xff,
                palette[i]>> 8 & 0xff,
                palette[i]     & 0xff,
            };
            const int d = diff(palrgb, rgb);
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

static void colormap_nearest_node(const struct color_node *map,
                                  const int node_pos,
                                  const uint8_t *target,
                                  struct nearest_color *nearest)
{
    const struct color_node *kd = map + node_pos;
    const int s = kd->split;
    int dx, nearer_kd_id, further_kd_id;
    const uint8_t *current = kd->val;
    const int current_to_target = diff(target, current);

    if (current_to_target < nearest->dist_sqd) {
        nearest->node_pos = node_pos;
        nearest->dist_sqd = current_to_target;
    }

    if (kd->left_id != -1 || kd->right_id != -1) {
        dx = target[s] - current[s];

        if (dx <= 0) nearer_kd_id = kd->left_id,  further_kd_id = kd->right_id;
        else         nearer_kd_id = kd->right_id, further_kd_id = kd->left_id;

        if (nearer_kd_id != -1)
            colormap_nearest_node(map, nearer_kd_id, target, nearest);

        if (further_kd_id != -1 && dx*dx < nearest->dist_sqd)
            colormap_nearest_node(map, further_kd_id, target, nearest);
    }
}

static av_always_inline uint8_t colormap_nearest_recursive(const struct color_node *node, const uint8_t *rgb)
{
    struct nearest_color res = {.dist_sqd = INT_MAX, .node_pos = -1};
    colormap_nearest_node(node, 0, rgb, &res);
    return node[res.node_pos].palette_id;
}

struct stack_node {
    int color_id;
    int dx2;
};

static av_always_inline uint8_t colormap_nearest_iterative(const struct color_node *root, const uint8_t *target)
{
    int pos = 0, best_node_id = -1, best_dist = INT_MAX, cur_color_id = 0;
    struct stack_node nodes[16];
    struct stack_node *node = &nodes[0];

    for (;;) {

        const struct color_node *kd = &root[cur_color_id];
        const uint8_t *current = kd->val;
        const int current_to_target = diff(target, current);

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

#define COLORMAP_NEAREST(search, palette, root, target)                                    \
    search == COLOR_SEARCH_NNS_ITERATIVE ? colormap_nearest_iterative(root, target) :      \
    search == COLOR_SEARCH_NNS_RECURSIVE ? colormap_nearest_recursive(root, target) :      \
                                           colormap_nearest_bruteforce(palette, target)

/**
 * Check if the requested color is in the cache already. If not, find it in the
 * color tree and cache it.
 * Note: r, g, and b are the component of c but are passed as well to avoid
 * recomputing them (they are generally computed by the caller for other uses).
 */
static av_always_inline int color_get(struct cache_node *cache, uint32_t color,
                                      uint8_t r, uint8_t g, uint8_t b,
                                      const struct color_node *map,
                                      const uint32_t *palette,
                                      const enum color_search_method search_method)
{
    int i;
    const uint8_t rgb[] = {r, g, b};
    const uint8_t rhash = r & ((1<<NBITS)-1);
    const uint8_t ghash = g & ((1<<NBITS)-1);
    const uint8_t bhash = b & ((1<<NBITS)-1);
    const unsigned hash = rhash<<(NBITS*2) | ghash<<NBITS | bhash;
    struct cache_node *node = &cache[hash];
    struct cached_color *e;

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
    e->pal_entry = COLORMAP_NEAREST(search_method, palette, map, rgb);
    return e->pal_entry;
}

static av_always_inline int get_dst_color_err(struct cache_node *cache,
                                              uint32_t c, const struct color_node *map,
                                              const uint32_t *palette,
                                              int *er, int *eg, int *eb,
                                              const enum color_search_method search_method)
{
    const uint8_t r = c >> 16 & 0xff;
    const uint8_t g = c >>  8 & 0xff;
    const uint8_t b = c       & 0xff;
    const int dstx = color_get(cache, c, r, g, b, map, palette, search_method);
    const uint32_t dstc = palette[dstx];
    *er = r - (dstc >> 16 & 0xff);
    *eg = g - (dstc >>  8 & 0xff);
    *eb = b - (dstc       & 0xff);
    return dstx;
}

static av_always_inline int set_frame(PaletteUseContext *s, AVFrame *out, AVFrame *in,
                                      int x_start, int y_start, int w, int h,
                                      enum dithering_mode dither,
                                      const enum color_search_method search_method)
{
    int x, y;
    const struct color_node *map = s->map;
    struct cache_node *cache = s->cache;
    const uint32_t *palette = s->palette;
    const int src_linesize = in ->linesize[0] >> 2;
    const int dst_linesize = out->linesize[0];
    uint32_t *src = ((uint32_t *)in ->data[0]) + y_start*src_linesize;
    uint8_t  *dst =              out->data[0]  + y_start*dst_linesize;

    w += x_start;
    h += y_start;

    for (y = y_start; y < h; y++) {
        for (x = x_start; x < w; x++) {
            int er, eg, eb;

            if (dither == DITHERING_BAYER) {
                const int d = s->ordered_dither[(y & 7)<<3 | (x & 7)];
                const uint8_t r8 = src[x] >> 16 & 0xff;
                const uint8_t g8 = src[x] >>  8 & 0xff;
                const uint8_t b8 = src[x]       & 0xff;
                const uint8_t r = av_clip_uint8(r8 + d);
                const uint8_t g = av_clip_uint8(g8 + d);
                const uint8_t b = av_clip_uint8(b8 + d);
                const uint32_t c = r<<16 | g<<8 | b;
                const int color = color_get(cache, c, r, g, b, map, palette, search_method);

                if (color < 0)
                    return color;
                dst[x] = color;

            } else if (dither == DITHERING_HECKBERT) {
                const int right = x < w - 1, down = y < h - 1;
                const int color = get_dst_color_err(cache, src[x], map, palette, &er, &eg, &eb, search_method);

                if (color < 0)
                    return color;
                dst[x] = color;

                if (right)         src[               x + 1] = dither_color(src[               x + 1], er, eg, eb, 3, 3);
                if (         down) src[src_linesize + x    ] = dither_color(src[src_linesize + x    ], er, eg, eb, 3, 3);
                if (right && down) src[src_linesize + x + 1] = dither_color(src[src_linesize + x + 1], er, eg, eb, 2, 3);

            } else if (dither == DITHERING_FLOYD_STEINBERG) {
                const int right = x < w - 1, down = y < h - 1, left = x > x_start;
                const int color = get_dst_color_err(cache, src[x], map, palette, &er, &eg, &eb, search_method);

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
                const int color = get_dst_color_err(cache, src[x], map, palette, &er, &eg, &eb, search_method);

                if (color < 0)
                    return color;
                dst[x] = color;

                if (right)          src[                 x + 1] = dither_color(src[                 x + 1], er, eg, eb, 4, 4);
                if (right2)         src[                 x + 2] = dither_color(src[                 x + 2], er, eg, eb, 3, 4);

                if (down) {
                    if (left2)      src[  src_linesize + x - 2] = dither_color(src[  src_linesize + x - 2], er, eg, eb, 1, 4);
                    if (left)       src[  src_linesize + x - 1] = dither_color(src[  src_linesize + x - 1], er, eg, eb, 2, 4);
                                    src[  src_linesize + x    ] = dither_color(src[  src_linesize + x    ], er, eg, eb, 3, 4);
                    if (right)      src[  src_linesize + x + 1] = dither_color(src[  src_linesize + x + 1], er, eg, eb, 2, 4);
                    if (right2)     src[  src_linesize + x + 2] = dither_color(src[  src_linesize + x + 2], er, eg, eb, 1, 4);
                }

            } else if (dither == DITHERING_SIERRA2_4A) {
                const int right = x < w - 1, down = y < h - 1, left = x > x_start;
                const int color = get_dst_color_err(cache, src[x], map, palette, &er, &eg, &eb, search_method);

                if (color < 0)
                    return color;
                dst[x] = color;

                if (right)         src[               x + 1] = dither_color(src[               x + 1], er, eg, eb, 2, 2);
                if (left  && down) src[src_linesize + x - 1] = dither_color(src[src_linesize + x - 1], er, eg, eb, 1, 2);
                if (         down) src[src_linesize + x    ] = dither_color(src[src_linesize + x    ], er, eg, eb, 1, 2);

            } else {
                const uint8_t r = src[x] >> 16 & 0xff;
                const uint8_t g = src[x] >>  8 & 0xff;
                const uint8_t b = src[x]       & 0xff;
                const int color = color_get(cache, src[x] & 0xffffff, r, g, b, map, palette, search_method);

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
    const uint32_t fontcolor = node->val[0] > 0x50 &&
                               node->val[1] > 0x50 &&
                               node->val[2] > 0x50 ? 0 : 0xffffff;
    av_bprintf(buf, "%*cnode%d ["
               "label=\"%c%02X%c%02X%c%02X%c\" "
               "fillcolor=\"#%02x%02x%02x\" "
               "fontcolor=\"#%06X\"]\n",
               depth*INDENT, ' ', node->palette_id,
               "[  "[node->split], node->val[0],
               "][ "[node->split], node->val[1],
               " ]["[node->split], node->val[2],
               "  ]"[node->split],
               node->val[0], node->val[1], node->val[2],
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
    FILE *f = av_fopen_utf8(fname, "w");

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

static int debug_accuracy(const struct color_node *node, const uint32_t *palette,
                          const enum color_search_method search_method)
{
    int r, g, b, ret = 0;

    for (r = 0; r < 256; r++) {
        for (g = 0; g < 256; g++) {
            for (b = 0; b < 256; b++) {
                const uint8_t rgb[] = {r, g, b};
                const int r1 = COLORMAP_NEAREST(search_method, palette, node, rgb);
                const int r2 = colormap_nearest_bruteforce(palette, rgb);
                if (r1 != r2) {
                    const uint32_t c1 = palette[r1];
                    const uint32_t c2 = palette[r2];
                    const uint8_t palrgb1[] = { c1>>16 & 0xff, c1>> 8 & 0xff, c1 & 0xff };
                    const uint8_t palrgb2[] = { c2>>16 & 0xff, c2>> 8 & 0xff, c2 & 0xff };
                    const int d1 = diff(palrgb1, rgb);
                    const int d2 = diff(palrgb2, rgb);
                    if (d1 != d2) {
                        av_log(NULL, AV_LOG_ERROR,
                               "/!\\ %02X%02X%02X: %d ! %d (%06X ! %06X) / dist: %d ! %d\n",
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
    uint8_t min[3];
    uint8_t max[3];
};

typedef int (*cmp_func)(const void *, const void *);

#define DECLARE_CMP_FUNC(name, pos)                     \
static int cmp_##name(const void *pa, const void *pb)   \
{                                                       \
    const struct color *a = pa;                         \
    const struct color *b = pb;                         \
    return   (a->value >> (8 * (2 - (pos))) & 0xff)     \
           - (b->value >> (8 * (2 - (pos))) & 0xff);    \
}

DECLARE_CMP_FUNC(r, 0)
DECLARE_CMP_FUNC(g, 1)
DECLARE_CMP_FUNC(b, 2)

static const cmp_func cmp_funcs[] = {cmp_r, cmp_g, cmp_b};

static int get_next_color(const uint8_t *color_used, const uint32_t *palette,
                          int *component, const struct color_rect *box)
{
    int wr, wg, wb;
    int i, longest = 0;
    unsigned nb_color = 0;
    struct color_rect ranges;
    struct color tmp_pal[256];
    cmp_func cmpf;

    ranges.min[0] = ranges.min[1] = ranges.min[2] = 0xff;
    ranges.max[0] = ranges.max[1] = ranges.max[2] = 0x00;

    for (i = 0; i < AVPALETTE_COUNT; i++) {
        const uint32_t c = palette[i];
        const uint8_t r = c >> 16 & 0xff;
        const uint8_t g = c >>  8 & 0xff;
        const uint8_t b = c       & 0xff;

        if (color_used[i] ||
            r < box->min[0] || g < box->min[1] || b < box->min[2] ||
            r > box->max[0] || g > box->max[1] || b > box->max[2])
            continue;

        if (r < ranges.min[0]) ranges.min[0] = r;
        if (g < ranges.min[1]) ranges.min[1] = g;
        if (b < ranges.min[2]) ranges.min[2] = b;

        if (r > ranges.max[0]) ranges.max[0] = r;
        if (g > ranges.max[1]) ranges.max[1] = g;
        if (b > ranges.max[2]) ranges.max[2] = b;

        tmp_pal[nb_color].value  = c;
        tmp_pal[nb_color].pal_id = i;

        nb_color++;
    }

    if (!nb_color)
        return -1;

    /* define longest axis that will be the split component */
    wr = ranges.max[0] - ranges.min[0];
    wg = ranges.max[1] - ranges.min[1];
    wb = ranges.max[2] - ranges.min[2];
    if (wr >= wg && wr >= wb) longest = 0;
    if (wg >= wr && wg >= wb) longest = 1;
    if (wb >= wr && wb >= wg) longest = 2;
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
                           const struct color_rect *box)
{
    uint32_t c;
    int component, cur_id;
    int node_left_id = -1, node_right_id = -1;
    struct color_node *node;
    struct color_rect box1, box2;
    const int pal_id = get_next_color(color_used, palette, &component, box);

    if (pal_id < 0)
        return -1;

    /* create new node with that color */
    cur_id = (*nb_used)++;
    c = palette[pal_id];
    node = &map[cur_id];
    node->split = component;
    node->palette_id = pal_id;
    node->val[0] = c>>16 & 0xff;
    node->val[1] = c>> 8 & 0xff;
    node->val[2] = c     & 0xff;

    color_used[pal_id] = 1;

    /* get the two boxes this node creates */
    box1 = box2 = *box;
    box1.max[component] = node->val[component];
    box2.min[component] = node->val[component] + 1;

    node_left_id = colormap_insert(map, color_used, nb_used, palette, &box1);

    if (box2.min[component] <= box2.max[component])
        node_right_id = colormap_insert(map, color_used, nb_used, palette, &box2);

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
    int i, nb_used = 0;
    uint8_t color_used[AVPALETTE_COUNT] = {0};
    uint32_t last_color = 0;
    struct color_rect box;

    /* disable transparent colors and dups */
    qsort(s->palette, AVPALETTE_COUNT, sizeof(*s->palette), cmp_pal_entry);
    for (i = 0; i < AVPALETTE_COUNT; i++) {
        const uint32_t c = s->palette[i];
        if (i != 0 && c == last_color) {
            color_used[i] = 1;
            continue;
        }
        last_color = c;
        if ((c & 0xff000000) != 0xff000000) {
            color_used[i] = 1; // ignore transparent color(s)
            continue;
        }
    }

    box.min[0] = box.min[1] = box.min[2] = 0x00;
    box.max[0] = box.max[1] = box.max[2] = 0xff;

    colormap_insert(s->map, color_used, &nb_used, s->palette, &box);

    if (s->dot_filename)
        disp_tree(s->map, s->dot_filename);

    if (s->debug_accuracy) {
        if (!debug_accuracy(s->map, s->palette, s->color_search_method))
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
    const float div = in1->width * in1->height * 3;
    unsigned mean_err = 0;

    for (y = 0; y < in1->height; y++) {
        for (x = 0; x < in1->width; x++) {
            const uint32_t c1 = src1[x];
            const uint32_t c2 = palette[src2[x]];
            const uint8_t rgb1[] = {c1 >> 16 & 0xff, c1 >> 8 & 0xff, c1 & 0xff};
            const uint8_t rgb2[] = {c2 >> 16 & 0xff, c2 >> 8 & 0xff, c2 & 0xff};
            mean_err += diff(rgb1, rgb2);
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

    if (prv_src && diff_mode == DIFF_MODE_RECTANGLE) {
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

static AVFrame *apply_palette(AVFilterLink *inlink, AVFrame *in)
{
    int x, y, w, h;
    AVFilterContext *ctx = inlink->dst;
    PaletteUseContext *s = ctx->priv;
    AVFilterLink *outlink = inlink->dst->outputs[0];

    AVFrame *out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return NULL;
    }
    av_frame_copy_props(out, in);

    set_processing_window(s->diff_mode, s->last_in, in,
                          s->last_out, out, &x, &y, &w, &h);
    av_frame_free(&s->last_in);
    av_frame_free(&s->last_out);
    s->last_in  = av_frame_clone(in);
    s->last_out = av_frame_clone(out);
    if (!s->last_in || !s->last_out ||
        av_frame_make_writable(s->last_in) < 0) {
        av_frame_free(&in);
        av_frame_free(&out);
        return NULL;
    }

    av_dlog(ctx, "%dx%d rect: (%d;%d) -> (%d,%d) [area:%dx%d]\n",
            w, h, x, y, x+w, y+h, in->width, in->height);

    if (s->set_frame(s, out, in, x, y, w, h) < 0) {
        av_frame_free(&out);
        return NULL;
    }
    memcpy(out->data[1], s->palette, AVPALETTE_SIZE);
    if (s->calc_mean_err)
        debug_mean_error(s, in, out, inlink->frame_count);
    av_frame_free(&in);
    return out;
}

static int config_output(AVFilterLink *outlink)
{
    int ret;
    AVFilterContext *ctx = outlink->src;
    PaletteUseContext *s = ctx->priv;

    outlink->w = ctx->inputs[0]->w;
    outlink->h = ctx->inputs[0]->h;

    outlink->time_base = ctx->inputs[0]->time_base;
    if ((ret = ff_dualinput_init(ctx, &s->dinput)) < 0)
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

    i = 0;
    for (y = 0; y < palette_frame->height; y++) {
        for (x = 0; x < palette_frame->width; x++)
            s->palette[i++] = p[x];
        p += p_linesize;
    }

    load_colormap(s);

    s->palette_loaded = 1;
}

static AVFrame *load_apply_palette(AVFilterContext *ctx, AVFrame *main,
                                   const AVFrame *second)
{
    AVFilterLink *inlink = ctx->inputs[0];
    PaletteUseContext *s = ctx->priv;
    if (!s->palette_loaded) {
        load_palette(s, second);
    }
    return apply_palette(inlink, main);
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    PaletteUseContext *s = inlink->dst->priv;
    return ff_dualinput_filter_frame(&s->dinput, inlink, in);
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
    s->dinput.repeatlast = 1; // only 1 frame in the palette
    s->dinput.process    = load_apply_palette;

    s->set_frame = set_frame_lut[s->color_search_method][s->dither];

    if (s->dither == DITHERING_BAYER) {
        int i;
        const int delta = 1 << (5 - s->bayer_scale); // to avoid too much luma

        for (i = 0; i < FF_ARRAY_ELEMS(s->ordered_dither); i++)
            s->ordered_dither[i] = (dither_value(i) >> s->bayer_scale) - delta;
    }

    return 0;
}

static int request_frame(AVFilterLink *outlink)
{
    PaletteUseContext *s = outlink->src->priv;
    return ff_dualinput_request_frame(&s->dinput, outlink);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    int i;
    PaletteUseContext *s = ctx->priv;

    ff_dualinput_uninit(&s->dinput);
    for (i = 0; i < CACHE_SIZE; i++)
        av_freep(&s->cache[i].entries);
    av_frame_free(&s->last_in);
    av_frame_free(&s->last_out);
}

static const AVFilterPad paletteuse_inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_VIDEO,
        .filter_frame   = filter_frame,
        .needs_writable = 1, // for error diffusal dithering
    },{
        .name           = "palette",
        .type           = AVMEDIA_TYPE_VIDEO,
        .config_props   = config_input_palette,
        .filter_frame   = filter_frame,
    },
    { NULL }
};

static const AVFilterPad paletteuse_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
        .request_frame = request_frame,
    },
    { NULL }
};

AVFilter ff_vf_paletteuse = {
    .name          = "paletteuse",
    .description   = NULL_IF_CONFIG_SMALL("Use a palette to downsample an input video stream."),
    .priv_size     = sizeof(PaletteUseContext),
    .query_formats = query_formats,
    .init          = init,
    .uninit        = uninit,
    .inputs        = paletteuse_inputs,
    .outputs       = paletteuse_outputs,
    .priv_class    = &paletteuse_class,
};
