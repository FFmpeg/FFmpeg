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
 * Generate one palette for a whole video stream.
 */

#include "libavutil/avassert.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "libavutil/qsort.h"
#include "avfilter.h"
#include "internal.h"

/* Reference a color and how much it's used */
struct color_ref {
    uint32_t color;
    uint64_t count;
};

/* Store a range of colors */
struct range_box {
    uint32_t color;     // average color
    int64_t variance;   // overall variance of the box (how much the colors are spread)
    int start;          // index in PaletteGenContext->refs
    int len;            // number of referenced colors
    int sorted_by;      // whether range of colors is sorted by red (0), green (1) or blue (2)
};

struct hist_node {
    struct color_ref *entries;
    int nb_entries;
};

enum {
    STATS_MODE_ALL_FRAMES,
    STATS_MODE_DIFF_FRAMES,
    STATS_MODE_SINGLE_FRAMES,
    NB_STATS_MODE
};

#define NBITS 5
#define HIST_SIZE (1<<(3*NBITS))

typedef struct PaletteGenContext {
    const AVClass *class;

    int max_colors;
    int reserve_transparent;
    int stats_mode;

    AVFrame *prev_frame;                    // previous frame used for the diff stats_mode
    struct hist_node histogram[HIST_SIZE];  // histogram/hashtable of the colors
    struct color_ref **refs;                // references of all the colors used in the stream
    int nb_refs;                            // number of color references (or number of different colors)
    struct range_box boxes[256];            // define the segmentation of the colorspace (the final palette)
    int nb_boxes;                           // number of boxes (increase will segmenting them)
    int palette_pushed;                     // if the palette frame is pushed into the outlink or not
} PaletteGenContext;

#define OFFSET(x) offsetof(PaletteGenContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption palettegen_options[] = {
    { "max_colors", "set the maximum number of colors to use in the palette", OFFSET(max_colors), AV_OPT_TYPE_INT, {.i64=256}, 4, 256, FLAGS },
    { "reserve_transparent", "reserve a palette entry for transparency", OFFSET(reserve_transparent), AV_OPT_TYPE_BOOL, {.i64=1}, 0, 1, FLAGS },
    { "stats_mode", "set statistics mode", OFFSET(stats_mode), AV_OPT_TYPE_INT, {.i64=STATS_MODE_ALL_FRAMES}, 0, NB_STATS_MODE-1, FLAGS, "mode" },
        { "full", "compute full frame histograms", 0, AV_OPT_TYPE_CONST, {.i64=STATS_MODE_ALL_FRAMES}, INT_MIN, INT_MAX, FLAGS, "mode" },
        { "diff", "compute histograms only for the part that differs from previous frame", 0, AV_OPT_TYPE_CONST, {.i64=STATS_MODE_DIFF_FRAMES}, INT_MIN, INT_MAX, FLAGS, "mode" },
        { "single", "compute new histogram for each frame", 0, AV_OPT_TYPE_CONST, {.i64=STATS_MODE_SINGLE_FRAMES}, INT_MIN, INT_MAX, FLAGS, "mode" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(palettegen);

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat in_fmts[]  = {AV_PIX_FMT_RGB32, AV_PIX_FMT_NONE};
    static const enum AVPixelFormat out_fmts[] = {AV_PIX_FMT_RGB32, AV_PIX_FMT_NONE};
    int ret;

    if ((ret = ff_formats_ref(ff_make_format_list(in_fmts) , &ctx->inputs[0]->out_formats)) < 0)
        return ret;
    if ((ret = ff_formats_ref(ff_make_format_list(out_fmts), &ctx->outputs[0]->in_formats)) < 0)
        return ret;
    return 0;
}

typedef int (*cmp_func)(const void *, const void *);

#define DECLARE_CMP_FUNC(name, pos)                     \
static int cmp_##name(const void *pa, const void *pb)   \
{                                                       \
    const struct color_ref * const *a = pa;             \
    const struct color_ref * const *b = pb;             \
    return   ((*a)->color >> (8 * (2 - (pos))) & 0xff)  \
           - ((*b)->color >> (8 * (2 - (pos))) & 0xff); \
}

DECLARE_CMP_FUNC(r, 0)
DECLARE_CMP_FUNC(g, 1)
DECLARE_CMP_FUNC(b, 2)

static const cmp_func cmp_funcs[] = {cmp_r, cmp_g, cmp_b};

/**
 * Simple color comparison for sorting the final palette
 */
static int cmp_color(const void *a, const void *b)
{
    const struct range_box *box1 = a;
    const struct range_box *box2 = b;
    return FFDIFFSIGN(box1->color , box2->color);
}

static av_always_inline int diff(const uint32_t a, const uint32_t b)
{
    const uint8_t c1[] = {a >> 16 & 0xff, a >> 8 & 0xff, a & 0xff};
    const uint8_t c2[] = {b >> 16 & 0xff, b >> 8 & 0xff, b & 0xff};
    const int dr = c1[0] - c2[0];
    const int dg = c1[1] - c2[1];
    const int db = c1[2] - c2[2];
    return dr*dr + dg*dg + db*db;
}

/**
 * Find the next box to split: pick the one with the highest variance
 */
static int get_next_box_id_to_split(PaletteGenContext *s)
{
    int box_id, i, best_box_id = -1;
    int64_t max_variance = -1;

    if (s->nb_boxes == s->max_colors - s->reserve_transparent)
        return -1;

    for (box_id = 0; box_id < s->nb_boxes; box_id++) {
        struct range_box *box = &s->boxes[box_id];

        if (s->boxes[box_id].len >= 2) {

            if (box->variance == -1) {
                int64_t variance = 0;

                for (i = 0; i < box->len; i++) {
                    const struct color_ref *ref = s->refs[box->start + i];
                    variance += diff(ref->color, box->color) * ref->count;
                }
                box->variance = variance;
            }
            if (box->variance > max_variance) {
                best_box_id = box_id;
                max_variance = box->variance;
            }
        } else {
            box->variance = -1;
        }
    }
    return best_box_id;
}

/**
 * Get the 32-bit average color for the range of RGB colors enclosed in the
 * specified box. Takes into account the weight of each color.
 */
static uint32_t get_avg_color(struct color_ref * const *refs,
                              const struct range_box *box)
{
    int i;
    const int n = box->len;
    uint64_t r = 0, g = 0, b = 0, div = 0;

    for (i = 0; i < n; i++) {
        const struct color_ref *ref = refs[box->start + i];
        r += (ref->color >> 16 & 0xff) * ref->count;
        g += (ref->color >>  8 & 0xff) * ref->count;
        b += (ref->color       & 0xff) * ref->count;
        div += ref->count;
    }

    r = r / div;
    g = g / div;
    b = b / div;

    return 0xffU<<24 | r<<16 | g<<8 | b;
}

/**
 * Split given box in two at position n. The original box becomes the left part
 * of the split, and the new index box is the right part.
 */
static void split_box(PaletteGenContext *s, struct range_box *box, int n)
{
    struct range_box *new_box = &s->boxes[s->nb_boxes++];
    new_box->start     = n + 1;
    new_box->len       = box->start + box->len - new_box->start;
    new_box->sorted_by = box->sorted_by;
    box->len -= new_box->len;

    av_assert0(box->len     >= 1);
    av_assert0(new_box->len >= 1);

    box->color     = get_avg_color(s->refs, box);
    new_box->color = get_avg_color(s->refs, new_box);
    box->variance     = -1;
    new_box->variance = -1;
}

/**
 * Write the palette into the output frame.
 */
static void write_palette(AVFilterContext *ctx, AVFrame *out)
{
    const PaletteGenContext *s = ctx->priv;
    int x, y, box_id = 0;
    uint32_t *pal = (uint32_t *)out->data[0];
    const int pal_linesize = out->linesize[0] >> 2;
    uint32_t last_color = 0;

    for (y = 0; y < out->height; y++) {
        for (x = 0; x < out->width; x++) {
            if (box_id < s->nb_boxes) {
                pal[x] = s->boxes[box_id++].color;
                if ((x || y) && pal[x] == last_color)
                    av_log(ctx, AV_LOG_WARNING, "Dupped color: %08"PRIX32"\n", pal[x]);
                last_color = pal[x];
            } else {
                pal[x] = 0xff000000; // pad with black
            }
        }
        pal += pal_linesize;
    }

    if (s->reserve_transparent) {
        av_assert0(s->nb_boxes < 256);
        pal[out->width - pal_linesize - 1] = 0x0000ff00; // add a green transparent color
    }
}

/**
 * Crawl the histogram to get all the defined colors, and create a linear list
 * of them (each color reference entry is a pointer to the value in the
 * histogram/hash table).
 */
static struct color_ref **load_color_refs(const struct hist_node *hist, int nb_refs)
{
    int i, j, k = 0;
    struct color_ref **refs = av_malloc_array(nb_refs, sizeof(*refs));

    if (!refs)
        return NULL;

    for (j = 0; j < HIST_SIZE; j++) {
        const struct hist_node *node = &hist[j];

        for (i = 0; i < node->nb_entries; i++)
            refs[k++] = &node->entries[i];
    }

    return refs;
}

static double set_colorquant_ratio_meta(AVFrame *out, int nb_out, int nb_in)
{
    char buf[32];
    const double ratio = (double)nb_out / nb_in;
    snprintf(buf, sizeof(buf), "%f", ratio);
    av_dict_set(&out->metadata, "lavfi.color_quant_ratio", buf, 0);
    return ratio;
}

/**
 * Main function implementing the Median Cut Algorithm defined by Paul Heckbert
 * in Color Image Quantization for Frame Buffer Display (1982)
 */
static AVFrame *get_palette_frame(AVFilterContext *ctx)
{
    AVFrame *out;
    PaletteGenContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    double ratio;
    int box_id = 0;
    struct range_box *box;

    /* reference only the used colors from histogram */
    s->refs = load_color_refs(s->histogram, s->nb_refs);
    if (!s->refs) {
        av_log(ctx, AV_LOG_ERROR, "Unable to allocate references for %d different colors\n", s->nb_refs);
        return NULL;
    }

    /* create the palette frame */
    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out)
        return NULL;
    out->pts = 0;

    /* set first box for 0..nb_refs */
    box = &s->boxes[box_id];
    box->len = s->nb_refs;
    box->sorted_by = -1;
    box->color = get_avg_color(s->refs, box);
    box->variance = -1;
    s->nb_boxes = 1;

    while (box && box->len > 1) {
        int i, rr, gr, br, longest;
        uint64_t median, box_weight = 0;

        /* compute the box weight (sum all the weights of the colors in the
         * range) and its boundings */
        uint8_t min[3] = {0xff, 0xff, 0xff};
        uint8_t max[3] = {0x00, 0x00, 0x00};
        for (i = box->start; i < box->start + box->len; i++) {
            const struct color_ref *ref = s->refs[i];
            const uint32_t rgb = ref->color;
            const uint8_t r = rgb >> 16 & 0xff, g = rgb >> 8 & 0xff, b = rgb & 0xff;
            min[0] = FFMIN(r, min[0]), max[0] = FFMAX(r, max[0]);
            min[1] = FFMIN(g, min[1]), max[1] = FFMAX(g, max[1]);
            min[2] = FFMIN(b, min[2]), max[2] = FFMAX(b, max[2]);
            box_weight += ref->count;
        }

        /* define the axis to sort by according to the widest range of colors */
        rr = max[0] - min[0];
        gr = max[1] - min[1];
        br = max[2] - min[2];
        longest = 1; // pick green by default (the color the eye is the most sensitive to)
        if (br >= rr && br >= gr) longest = 2;
        if (rr >= gr && rr >= br) longest = 0;
        if (gr >= rr && gr >= br) longest = 1; // prefer green again

        ff_dlog(ctx, "box #%02X [%6d..%-6d] (%6d) w:%-6"PRIu64" ranges:[%2x %2x %2x] sort by %c (already sorted:%c) ",
                box_id, box->start, box->start + box->len - 1, box->len, box_weight,
                rr, gr, br, "rgb"[longest], box->sorted_by == longest ? 'y':'n');

        /* sort the range by its longest axis if it's not already sorted */
        if (box->sorted_by != longest) {
            cmp_func cmpf = cmp_funcs[longest];
            AV_QSORT(&s->refs[box->start], box->len, const struct color_ref *, cmpf);
            box->sorted_by = longest;
        }

        /* locate the median where to split */
        median = (box_weight + 1) >> 1;
        box_weight = 0;
        /* if you have 2 boxes, the maximum is actually #0: you must have at
         * least 1 color on each side of the split, hence the -2 */
        for (i = box->start; i < box->start + box->len - 2; i++) {
            box_weight += s->refs[i]->count;
            if (box_weight > median)
                break;
        }
        ff_dlog(ctx, "split @ i=%-6d with w=%-6"PRIu64" (target=%6"PRIu64")\n", i, box_weight, median);
        split_box(s, box, i);

        box_id = get_next_box_id_to_split(s);
        box = box_id >= 0 ? &s->boxes[box_id] : NULL;
    }

    ratio = set_colorquant_ratio_meta(out, s->nb_boxes, s->nb_refs);
    av_log(ctx, AV_LOG_INFO, "%d%s colors generated out of %d colors; ratio=%f\n",
           s->nb_boxes, s->reserve_transparent ? "(+1)" : "", s->nb_refs, ratio);

    qsort(s->boxes, s->nb_boxes, sizeof(*s->boxes), cmp_color);

    write_palette(ctx, out);

    return out;
}

/**
 * Hashing function for the color.
 * It keeps the NBITS least significant bit of each component to make it
 * "random" even if the scene doesn't have much different colors.
 */
static inline unsigned color_hash(uint32_t color)
{
    const uint8_t r = color >> 16 & ((1<<NBITS)-1);
    const uint8_t g = color >>  8 & ((1<<NBITS)-1);
    const uint8_t b = color       & ((1<<NBITS)-1);
    return r<<(NBITS*2) | g<<NBITS | b;
}

/**
 * Locate the color in the hash table and increment its counter.
 */
static int color_inc(struct hist_node *hist, uint32_t color)
{
    int i;
    const unsigned hash = color_hash(color);
    struct hist_node *node = &hist[hash];
    struct color_ref *e;

    for (i = 0; i < node->nb_entries; i++) {
        e = &node->entries[i];
        if (e->color == color) {
            e->count++;
            return 0;
        }
    }

    e = av_dynarray2_add((void**)&node->entries, &node->nb_entries,
                         sizeof(*node->entries), NULL);
    if (!e)
        return AVERROR(ENOMEM);
    e->color = color;
    e->count = 1;
    return 1;
}

/**
 * Update histogram when pixels differ from previous frame.
 */
static int update_histogram_diff(struct hist_node *hist,
                                 const AVFrame *f1, const AVFrame *f2)
{
    int x, y, ret, nb_diff_colors = 0;

    for (y = 0; y < f1->height; y++) {
        const uint32_t *p = (const uint32_t *)(f1->data[0] + y*f1->linesize[0]);
        const uint32_t *q = (const uint32_t *)(f2->data[0] + y*f2->linesize[0]);

        for (x = 0; x < f1->width; x++) {
            if (p[x] == q[x])
                continue;
            ret = color_inc(hist, p[x]);
            if (ret < 0)
                return ret;
            nb_diff_colors += ret;
        }
    }
    return nb_diff_colors;
}

/**
 * Simple histogram of the frame.
 */
static int update_histogram_frame(struct hist_node *hist, const AVFrame *f)
{
    int x, y, ret, nb_diff_colors = 0;

    for (y = 0; y < f->height; y++) {
        const uint32_t *p = (const uint32_t *)(f->data[0] + y*f->linesize[0]);

        for (x = 0; x < f->width; x++) {
            ret = color_inc(hist, p[x]);
            if (ret < 0)
                return ret;
            nb_diff_colors += ret;
        }
    }
    return nb_diff_colors;
}

/**
 * Update the histogram for each passing frame. No frame will be pushed here.
 */
static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    PaletteGenContext *s = ctx->priv;
    int ret = s->prev_frame ? update_histogram_diff(s->histogram, s->prev_frame, in)
                            : update_histogram_frame(s->histogram, in);

    if (ret > 0)
        s->nb_refs += ret;

    if (s->stats_mode == STATS_MODE_DIFF_FRAMES) {
        av_frame_free(&s->prev_frame);
        s->prev_frame = in;
    } else if (s->stats_mode == STATS_MODE_SINGLE_FRAMES) {
        AVFrame *out;
        int i;

        out = get_palette_frame(ctx);
        out->pts = in->pts;
        av_frame_free(&in);
        ret = ff_filter_frame(ctx->outputs[0], out);
        for (i = 0; i < HIST_SIZE; i++)
            av_freep(&s->histogram[i].entries);
        av_freep(&s->refs);
        s->nb_refs = 0;
        s->nb_boxes = 0;
        memset(s->boxes, 0, sizeof(s->boxes));
        memset(s->histogram, 0, sizeof(s->histogram));
    } else {
        av_frame_free(&in);
    }

    return ret;
}

/**
 * Returns only one frame at the end containing the full palette.
 */
static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = ctx->inputs[0];
    PaletteGenContext *s = ctx->priv;
    int r;

    r = ff_request_frame(inlink);
    if (r == AVERROR_EOF && !s->palette_pushed && s->nb_refs && s->stats_mode != STATS_MODE_SINGLE_FRAMES) {
        r = ff_filter_frame(outlink, get_palette_frame(ctx));
        s->palette_pushed = 1;
        return r;
    }
    return r;
}

/**
 * The output is one simple 16x16 squared-pixels palette.
 */
static int config_output(AVFilterLink *outlink)
{
    outlink->w = outlink->h = 16;
    outlink->sample_aspect_ratio = av_make_q(1, 1);
    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    int i;
    PaletteGenContext *s = ctx->priv;

    for (i = 0; i < HIST_SIZE; i++)
        av_freep(&s->histogram[i].entries);
    av_freep(&s->refs);
    av_frame_free(&s->prev_frame);
}

static const AVFilterPad palettegen_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad palettegen_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
        .request_frame = request_frame,
    },
    { NULL }
};

AVFilter ff_vf_palettegen = {
    .name          = "palettegen",
    .description   = NULL_IF_CONFIG_SMALL("Find the optimal palette for a given stream."),
    .priv_size     = sizeof(PaletteGenContext),
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = palettegen_inputs,
    .outputs       = palettegen_outputs,
    .priv_class    = &palettegen_class,
};
