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
 * Generate one palette for a whole video stream.
 */

#include "libavutil/avassert.h"
#include "libavutil/internal.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/intreadwrite.h"
#include "avfilter.h"
#include "filters.h"
#include "formats.h"
#include "palette.h"
#include "video.h"

/* Reference a color and how much it's used */
struct color_ref {
    uint32_t color;
    struct Lab lab;
    int64_t count;
};

/* Store a range of colors */
struct range_box {
    uint32_t color;     // average color
    struct Lab avg;     // average color in perceptual OkLab space
    int major_axis;     // best axis candidate for cutting the box
    int64_t weight;     // sum of all the weights of the colors
    int64_t cut_score;  // how likely the box is to be cut down (higher implying more likely)
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

#define HIST_SIZE (1<<15)

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
    uint8_t transparency_color[4];          // background color for transparency
} PaletteGenContext;

#define OFFSET(x) offsetof(PaletteGenContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption palettegen_options[] = {
    { "max_colors", "set the maximum number of colors to use in the palette", OFFSET(max_colors), AV_OPT_TYPE_INT, {.i64=256}, 2, 256, FLAGS },
    { "reserve_transparent", "reserve a palette entry for transparency", OFFSET(reserve_transparent), AV_OPT_TYPE_BOOL, {.i64=1}, 0, 1, FLAGS },
    { "transparency_color", "set a background color for transparency", OFFSET(transparency_color), AV_OPT_TYPE_COLOR, {.str="lime"}, 0, 0, FLAGS },
    { "stats_mode", "set statistics mode", OFFSET(stats_mode), AV_OPT_TYPE_INT, {.i64=STATS_MODE_ALL_FRAMES}, 0, NB_STATS_MODE-1, FLAGS, .unit = "mode" },
        { "full", "compute full frame histograms", 0, AV_OPT_TYPE_CONST, {.i64=STATS_MODE_ALL_FRAMES}, INT_MIN, INT_MAX, FLAGS, .unit = "mode" },
        { "diff", "compute histograms only for the part that differs from previous frame", 0, AV_OPT_TYPE_CONST, {.i64=STATS_MODE_DIFF_FRAMES}, INT_MIN, INT_MAX, FLAGS, .unit = "mode" },
        { "single", "compute new histogram for each frame", 0, AV_OPT_TYPE_CONST, {.i64=STATS_MODE_SINGLE_FRAMES}, INT_MIN, INT_MAX, FLAGS, .unit = "mode" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(palettegen);

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat in_fmts[]  = {AV_PIX_FMT_RGB32, AV_PIX_FMT_NONE};
    static const enum AVPixelFormat out_fmts[] = {AV_PIX_FMT_RGB32, AV_PIX_FMT_NONE};
    int ret;

    if ((ret = ff_formats_ref(ff_make_format_list(in_fmts) , &ctx->inputs[0]->outcfg.formats)) < 0)
        return ret;
    if ((ret = ff_formats_ref(ff_make_format_list(out_fmts), &ctx->outputs[0]->incfg.formats)) < 0)
        return ret;
    return 0;
}

typedef int (*cmp_func)(const void *, const void *);

#define DECLARE_CMP_FUNC(k0, k1, k2)                        \
static int cmp_##k0##k1##k2(const void *pa, const void *pb) \
{                                                           \
    const struct color_ref * const *a = pa;                 \
    const struct color_ref * const *b = pb;                 \
    const int c0 = FFDIFFSIGN((*a)->lab.k0, (*b)->lab.k0);  \
    const int c1 = FFDIFFSIGN((*a)->lab.k1, (*b)->lab.k1);  \
    const int c2 = FFDIFFSIGN((*a)->lab.k2, (*b)->lab.k2);  \
    return c0 ? c0 : c1 ? c1 : c2;                          \
}

DECLARE_CMP_FUNC(L, a, b)
DECLARE_CMP_FUNC(L, b, a)
DECLARE_CMP_FUNC(a, L, b)
DECLARE_CMP_FUNC(a, b, L)
DECLARE_CMP_FUNC(b, L, a)
DECLARE_CMP_FUNC(b, a, L)

enum { ID_XYZ, ID_XZY, ID_ZXY, ID_YXZ, ID_ZYX, ID_YZX };
static const char * const sortstr[] = { "Lab", "Lba", "bLa", "aLb", "baL", "abL" };

static const cmp_func cmp_funcs[] = {
    [ID_XYZ] = cmp_Lab,
    [ID_XZY] = cmp_Lba,
    [ID_ZXY] = cmp_bLa,
    [ID_YXZ] = cmp_aLb,
    [ID_ZYX] = cmp_baL,
    [ID_YZX] = cmp_abL,
};

/*
 * Return an identifier for the order of x, y, z (from higher to lower),
 * preferring x over y and y over z in case of equality.
 */
static int sort3id(int64_t x, int64_t y, int64_t z)
{
    if (x >= y) {
        if (y >= z) return ID_XYZ;
        if (x >= z) return ID_XZY;
        return ID_ZXY;
    }
    if (x >= z) return ID_YXZ;
    if (y >= z) return ID_YZX;
    return ID_ZYX;
}

/**
 * Simple color comparison for sorting the final palette
 */
static int cmp_color(const void *a, const void *b)
{
    const struct range_box *box1 = a;
    const struct range_box *box2 = b;
    return FFDIFFSIGN(box1->color, box2->color);
}

static void compute_box_stats(PaletteGenContext *s, struct range_box *box)
{
    int64_t er2[3] = {0};

    /* Compute average color */
    int64_t sL = 0, sa = 0, sb = 0;
    box->weight = 0;
    for (int i = box->start; i < box->start + box->len; i++) {
        const struct color_ref *ref = s->refs[i];
        sL += ref->lab.L * ref->count;
        sa += ref->lab.a * ref->count;
        sb += ref->lab.b * ref->count;
        box->weight += ref->count;
    }
    box->avg.L = sL / box->weight;
    box->avg.a = sa / box->weight;
    box->avg.b = sb / box->weight;

    /* Compute squared error of each color channel */
    for (int i = box->start; i < box->start + box->len; i++) {
        const struct color_ref *ref = s->refs[i];
        const int64_t dL = ref->lab.L - box->avg.L;
        const int64_t da = ref->lab.a - box->avg.a;
        const int64_t db = ref->lab.b - box->avg.b;
        er2[0] += dL * dL * ref->count;
        er2[1] += da * da * ref->count;
        er2[2] += db * db * ref->count;
    }

    /* Define the best axis candidate for cutting the box */
    box->major_axis = sort3id(er2[0], er2[1], er2[2]);

    /* The box that has the axis with the biggest error amongst all boxes will but cut down */
    box->cut_score = FFMAX3(er2[0], er2[1], er2[2]);
}

/**
 * Find the next box to split: pick the one with the highest cut score
 */
static int get_next_box_id_to_split(PaletteGenContext *s)
{
    int best_box_id = -1;
    int64_t max_score = -1;

    if (s->nb_boxes == s->max_colors - s->reserve_transparent)
        return -1;

    for (int box_id = 0; box_id < s->nb_boxes; box_id++) {
        const struct range_box *box = &s->boxes[box_id];
        if (s->boxes[box_id].len >= 2 && box->cut_score > max_score) {
            best_box_id = box_id;
            max_score = box->cut_score;
        }
    }
    return best_box_id;
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

    compute_box_stats(s, box);
    compute_box_stats(s, new_box);
}

/**
 * Write the palette into the output frame.
 */
static void write_palette(AVFilterContext *ctx, AVFrame *out)
{
    const PaletteGenContext *s = ctx->priv;
    int box_id = 0;
    uint32_t *pal = (uint32_t *)out->data[0];
    const int pal_linesize = out->linesize[0] >> 2;
    uint32_t last_color = 0;

    for (int y = 0; y < out->height; y++) {
        for (int x = 0; x < out->width; x++) {
            if (box_id < s->nb_boxes) {
                pal[x] = s->boxes[box_id++].color;
                if ((x || y) && pal[x] == last_color)
                    av_log(ctx, AV_LOG_WARNING, "Duped color: %08"PRIX32"\n", pal[x]);
                last_color = pal[x];
            } else {
                pal[x] = last_color; // pad with last color
            }
        }
        pal += pal_linesize;
    }

    if (s->reserve_transparent) {
        av_assert0(s->nb_boxes < 256);
        pal[out->width - pal_linesize - 1] = AV_RB32(&s->transparency_color) >> 8;
    }
}

/**
 * Crawl the histogram to get all the defined colors, and create a linear list
 * of them (each color reference entry is a pointer to the value in the
 * histogram/hash table).
 */
static struct color_ref **load_color_refs(const struct hist_node *hist, int nb_refs)
{
    int k = 0;
    struct color_ref **refs = av_malloc_array(nb_refs, sizeof(*refs));

    if (!refs)
        return NULL;

    for (int j = 0; j < HIST_SIZE; j++) {
        const struct hist_node *node = &hist[j];

        for (int i = 0; i < node->nb_entries; i++)
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
    compute_box_stats(s, box);
    s->nb_boxes = 1;

    while (box && box->len > 1) {
        int i;
        int64_t median, weight;

        ff_dlog(ctx, "box #%02X [%6d..%-6d] (%6d) w:%-6"PRIu64" sort by %s (already sorted:%c) ",
                box_id, box->start, box->start + box->len - 1, box->len, box->weight,
                sortstr[box->major_axis], box->sorted_by == box->major_axis ? 'y':'n');

        /* sort the range by its major axis if it's not already sorted */
        if (box->sorted_by != box->major_axis) {
            cmp_func cmpf = cmp_funcs[box->major_axis];
            qsort(&s->refs[box->start], box->len, sizeof(struct color_ref *), cmpf);
            box->sorted_by = box->major_axis;
        }

        /* locate the median where to split */
        median = (box->weight + 1) >> 1;
        weight = 0;
        /* if you have 2 boxes, the maximum is actually #0: you must have at
         * least 1 color on each side of the split, hence the -2 */
        for (i = box->start; i < box->start + box->len - 2; i++) {
            weight += s->refs[i]->count;
            if (weight > median)
                break;
        }
        ff_dlog(ctx, "split @ i=%-6d with w=%-6"PRIu64" (target=%6"PRIu64")\n", i, weight, median);
        split_box(s, box, i);

        box_id = get_next_box_id_to_split(s);
        box = box_id >= 0 ? &s->boxes[box_id] : NULL;
    }

    ratio = set_colorquant_ratio_meta(out, s->nb_boxes, s->nb_refs);
    av_log(ctx, AV_LOG_INFO, "%d%s colors generated out of %d colors; ratio=%f\n",
           s->nb_boxes, s->reserve_transparent ? "(+1)" : "", s->nb_refs, ratio);

    for (int i = 0; i < s->nb_boxes; i++)
        s->boxes[i].color = 0xffU<<24 | ff_oklab_int_to_srgb_u8(s->boxes[i].avg);

    qsort(s->boxes, s->nb_boxes, sizeof(*s->boxes), cmp_color);

    write_palette(ctx, out);

    return out;
}

/**
 * Locate the color in the hash table and increment its counter.
 */
static int color_inc(struct hist_node *hist, uint32_t color)
{
    const uint32_t hash = ff_lowbias32(color) & (HIST_SIZE - 1);
    struct hist_node *node = &hist[hash];
    struct color_ref *e;

    for (int i = 0; i < node->nb_entries; i++) {
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
    e->lab = ff_srgb_u8_to_oklab_int(color);
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
    int ret;

    if (in->color_trc != AVCOL_TRC_UNSPECIFIED && in->color_trc != AVCOL_TRC_IEC61966_2_1)
        av_log(ctx, AV_LOG_WARNING, "The input frame is not in sRGB, colors may be off\n");

    ret = s->prev_frame ? update_histogram_diff(s->histogram, s->prev_frame, in)
                        : update_histogram_frame(s->histogram, in);
    if (ret > 0)
        s->nb_refs += ret;

    if (s->stats_mode == STATS_MODE_DIFF_FRAMES) {
        av_frame_free(&s->prev_frame);
        s->prev_frame = in;
    } else if (s->stats_mode == STATS_MODE_SINGLE_FRAMES && s->nb_refs > 0) {
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

static int init(AVFilterContext *ctx)
{
    PaletteGenContext* s = ctx->priv;

    if (s->max_colors - s->reserve_transparent < 2) {
        av_log(ctx, AV_LOG_ERROR, "max_colors=2 is only allowed without reserving a transparent color slot\n");
        return AVERROR(EINVAL);
    }

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
};

static const AVFilterPad palettegen_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
        .request_frame = request_frame,
    },
};

const AVFilter ff_vf_palettegen = {
    .name          = "palettegen",
    .description   = NULL_IF_CONFIG_SMALL("Find the optimal palette for a given stream."),
    .priv_size     = sizeof(PaletteGenContext),
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(palettegen_inputs),
    FILTER_OUTPUTS(palettegen_outputs),
    FILTER_QUERY_FUNC(query_formats),
    .priv_class    = &palettegen_class,
};
