/*
 * Copyright (c) 2003 Rich Felker
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"
#include "vf_pullup.h"

#define F_HAVE_BREAKS   1
#define F_HAVE_AFFINITY 2

#define BREAK_LEFT  1
#define BREAK_RIGHT 2

#define OFFSET(x) offsetof(PullupContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption pullup_options[] = {
    { "jl", "set left junk size",  OFFSET(junk_left),  AV_OPT_TYPE_INT, {.i64=1}, 0, INT_MAX, FLAGS },
    { "jr", "set right junk size", OFFSET(junk_right), AV_OPT_TYPE_INT, {.i64=1}, 0, INT_MAX, FLAGS },
    { "jt", "set top junk size",   OFFSET(junk_top),   AV_OPT_TYPE_INT, {.i64=4}, 1, INT_MAX, FLAGS },
    { "jb", "set bottom junk size", OFFSET(junk_bottom), AV_OPT_TYPE_INT, {.i64=4}, 1, INT_MAX, FLAGS },
    { "sb", "set strict breaks", OFFSET(strict_breaks), AV_OPT_TYPE_INT, {.i64=0},-1, 1, FLAGS },
    { "mp", "set metric plane",  OFFSET(metric_plane),  AV_OPT_TYPE_INT, {.i64=0}, 0, 2, FLAGS, "mp" },
    { "y", "luma",        0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, FLAGS, "mp" },
    { "u", "chroma blue", 0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, FLAGS, "mp" },
    { "v", "chroma red",  0, AV_OPT_TYPE_CONST, {.i64=2}, 0, 0, FLAGS, "mp" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(pullup);

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ440P,
        AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ420P,
        AV_PIX_FMT_YUV444P,  AV_PIX_FMT_YUV440P,
        AV_PIX_FMT_YUV422P,  AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_YUV411P,  AV_PIX_FMT_YUV410P,
        AV_PIX_FMT_YUVJ411P, AV_PIX_FMT_GRAY8,
        AV_PIX_FMT_NONE
    };
    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
    return 0;
}

#define ABS(a) (((a) ^ ((a) >> 31)) - ((a) >> 31))

static int diff_c(const uint8_t *a, const uint8_t *b, int s)
{
    int i, j, diff = 0;

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 8; j++)
            diff += ABS(a[j] - b[j]);
        a += s;
        b += s;
    }

    return diff;
}

static int comb_c(const uint8_t *a, const uint8_t *b, int s)
{
    int i, j, comb = 0;

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 8; j++)
            comb += ABS((a[j] << 1) - b[j - s] - b[j    ]) +
                    ABS((b[j] << 1) - a[j    ] - a[j + s]);
        a += s;
        b += s;
    }

    return comb;
}

static int var_c(const uint8_t *a, const uint8_t *b, int s)
{
    int i, j, var = 0;

    for (i = 0; i < 3; i++) {
        for (j = 0; j < 8; j++)
            var += ABS(a[j] - a[j + s]);
        a += s;
    }

    return 4 * var; /* match comb scaling */
}

static int alloc_metrics(PullupContext *s, PullupField *f)
{
    f->diffs = av_calloc(FFALIGN(s->metric_length, 16), sizeof(*f->diffs));
    f->combs = av_calloc(FFALIGN(s->metric_length, 16), sizeof(*f->combs));
    f->vars  = av_calloc(FFALIGN(s->metric_length, 16), sizeof(*f->vars));

    if (!f->diffs || !f->combs || !f->vars) {
        av_freep(&f->diffs);
        av_freep(&f->combs);
        av_freep(&f->vars);
        return AVERROR(ENOMEM);
    }
    return 0;
}

static PullupField *make_field_queue(PullupContext *s, int len)
{
    PullupField *head, *f;

    f = head = av_mallocz(sizeof(*head));
    if (!f)
        return NULL;

    if (alloc_metrics(s, f) < 0) {
        av_free(f);
        return NULL;
    }

    for (; len > 0; len--) {
        f->next = av_mallocz(sizeof(*f->next));
        if (!f->next)
            return NULL;

        f->next->prev = f;
        f = f->next;
        if (alloc_metrics(s, f) < 0)
            return NULL;
    }

    f->next = head;
    head->prev = f;

    return head;
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    PullupContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int mp = s->metric_plane;

    s->nb_planes = av_pix_fmt_count_planes(inlink->format);

    if (mp + 1 > s->nb_planes) {
        av_log(ctx, AV_LOG_ERROR, "input format does not have such plane\n");
        return AVERROR(EINVAL);
    }

    s->planeheight[1] = s->planeheight[2] = FF_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;
    s->planewidth[1]  = s->planewidth[2]  = FF_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->planewidth[0]  = s->planewidth[3]  = inlink->w;

    s->metric_w      = (s->planewidth[mp]  - ((s->junk_left + s->junk_right)  << 3)) >> 3;
    s->metric_h      = (s->planeheight[mp] - ((s->junk_top  + s->junk_bottom) << 1)) >> 3;
    s->metric_offset = (s->junk_left << 3) + (s->junk_top << 1) * s->planewidth[mp];
    s->metric_length = s->metric_w * s->metric_h;

    av_log(ctx, AV_LOG_DEBUG, "w: %d h: %d\n", s->metric_w, s->metric_h);
    av_log(ctx, AV_LOG_DEBUG, "offset: %d length: %d\n", s->metric_offset, s->metric_length);

    s->head = make_field_queue(s, 8);
    if (!s->head)
        return AVERROR(ENOMEM);

    s->diff = diff_c;
    s->comb = comb_c;
    s->var  = var_c;

    if (ARCH_X86)
        ff_pullup_init_x86(s);
    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    outlink->flags |= FF_LINK_FLAG_REQUEST_LOOP;
    return 0;
}

static PullupBuffer *pullup_lock_buffer(PullupBuffer *b, int parity)
{
    if (!b)
        return NULL;

    if ((parity + 1) & 1)
        b->lock[0]++;
    if ((parity + 1) & 2)
        b->lock[1]++;

    return b;
}

static void pullup_release_buffer(PullupBuffer *b, int parity)
{
    if (!b)
        return;

    if ((parity + 1) & 1)
        b->lock[0]--;
    if ((parity + 1) & 2)
        b->lock[1]--;
}

static int alloc_buffer(PullupContext *s, PullupBuffer *b)
{
    int i;

    if (b->planes[0])
        return 0;
    for (i = 0; i < s->nb_planes; i++) {
        b->planes[i] = av_malloc(s->planeheight[i] * s->planewidth[i]);
    }

    return 0;
}

static PullupBuffer *pullup_get_buffer(PullupContext *s, int parity)
{
    int i;

    /* Try first to get the sister buffer for the previous field */
    if (parity < 2 && s->last && parity != s->last->parity
        && !s->last->buffer->lock[parity]) {
        alloc_buffer(s, s->last->buffer);
        return pullup_lock_buffer(s->last->buffer, parity);
    }

    /* Prefer a buffer with both fields open */
    for (i = 0; i < FF_ARRAY_ELEMS(s->buffers); i++) {
        if (s->buffers[i].lock[0])
            continue;
        if (s->buffers[i].lock[1])
            continue;
        alloc_buffer(s, &s->buffers[i]);
        return pullup_lock_buffer(&s->buffers[i], parity);
    }

    if (parity == 2)
        return 0;

    /* Search for any half-free buffer */
    for (i = 0; i < FF_ARRAY_ELEMS(s->buffers); i++) {
        if (((parity + 1) & 1) && s->buffers[i].lock[0])
            continue;
        if (((parity + 1) & 2) && s->buffers[i].lock[1])
            continue;
        alloc_buffer(s, &s->buffers[i]);
        return pullup_lock_buffer(&s->buffers[i], parity);
    }

    return NULL;
}

static int queue_length(PullupField *begin, PullupField *end)
{
    PullupField *f;
    int count = 1;

    if (!begin || !end)
        return 0;

    for (f = begin; f != end; f = f->next)
        count++;

    return count;
}

static int find_first_break(PullupField *f, int max)
{
    int i;

    for (i = 0; i < max; i++) {
        if (f->breaks & BREAK_RIGHT || f->next->breaks & BREAK_LEFT)
            return i + 1;
        f = f->next;
    }

    return 0;
}

static void compute_breaks(PullupContext *s, PullupField *f0)
{
    PullupField *f1 = f0->next;
    PullupField *f2 = f1->next;
    PullupField *f3 = f2->next;
    int i, l, max_l = 0, max_r = 0;

    if (f0->flags & F_HAVE_BREAKS)
        return;

    f0->flags |= F_HAVE_BREAKS;

    /* Special case when fields are 100% identical */
    if (f0->buffer == f2->buffer && f1->buffer != f3->buffer) {
        f2->breaks |= BREAK_RIGHT;
        return;
    }

    if (f0->buffer != f2->buffer && f1->buffer == f3->buffer) {
        f1->breaks |= BREAK_LEFT;
        return;
    }

    for (i = 0; i < s->metric_length; i++) {
        l = f2->diffs[i] - f3->diffs[i];

        if ( l > max_l)
            max_l =  l;
        if (-l > max_r)
            max_r = -l;
    }

    /* Don't get tripped up when differences are mostly quant error */
    if (max_l + max_r < 128)
        return;
    if (max_l > 4 * max_r)
        f1->breaks |= BREAK_LEFT;
    if (max_r > 4 * max_l)
        f2->breaks |= BREAK_RIGHT;
}

static void compute_affinity(PullupContext *s, PullupField *f)
{
    int i, max_l = 0, max_r = 0, l;

    if (f->flags & F_HAVE_AFFINITY)
        return;

    f->flags |= F_HAVE_AFFINITY;

    if (f->buffer == f->next->next->buffer) {
        f->affinity             =  1;
        f->next->affinity       =  0;
        f->next->next->affinity = -1;
        f->next->flags         |= F_HAVE_AFFINITY;
        f->next->next->flags   |= F_HAVE_AFFINITY;
        return;
    }

    for (i = 0; i < s->metric_length; i++) {
        int v  = f->vars[i];
        int lv = f->prev->vars[i];
        int rv = f->next->vars[i];
        int lc = f->combs[i] - (v + lv) + ABS(v - lv);
        int rc = f->next->combs[i] - (v + rv) + ABS(v - rv);

        lc = FFMAX(lc, 0);
        rc = FFMAX(rc, 0);
        l  = lc - rc;

        if ( l > max_l)
            max_l =  l;
        if (-l > max_r)
            max_r = -l;
    }

    if (max_l + max_r < 64)
        return;

    if (max_r > 6 * max_l)
        f->affinity = -1;
    else if (max_l > 6 * max_r)
        f->affinity =  1;
}

static int decide_frame_length(PullupContext *s)
{
    PullupField *f0 = s->first;
    PullupField *f1 = f0->next;
    PullupField *f2 = f1->next;
    PullupField *f;
    int i, l, n;

    if (queue_length(s->first, s->last) < 4)
        return 0;

    f = s->first;
    n = queue_length(f, s->last);
    for (i = 0; i < n - 1; i++) {
        if (i < n - 3)
            compute_breaks(s, f);

        compute_affinity(s, f);

        f = f->next;
    }

    if (f0->affinity == -1)
        return 1;

    l = find_first_break(f0, 3);

    if (l == 1 && s->strict_breaks < 0)
        l = 0;

    switch (l) {
    case 1:
        return 1 + (s->strict_breaks < 1 && f0->affinity == 1 && f1->affinity == -1);
    case 2:
        /* FIXME: strictly speaking, f0->prev is no longer valid... :) */
        if (s->strict_pairs
            && (f0->prev->breaks & BREAK_RIGHT) && (f2->breaks & BREAK_LEFT)
            && (f0->affinity != 1 || f1->affinity != -1) )
            return 1;
        return 1 + (f1->affinity != 1);
    case 3:
        return 2 + (f2->affinity != 1);
    default:
        /* 9 possibilities covered before switch */
        if (f1->affinity == 1)
            return 1; /* covers 6 */
        else if (f1->affinity == -1)
            return 2; /* covers 6 */
        else if (f2->affinity == -1) { /* covers 2 */
            return (f0->affinity == 1) ? 3 : 1;
        } else {
            return 2; /* the remaining 6 */
        }
    }
}

static PullupFrame *pullup_get_frame(PullupContext *s)
{
    PullupFrame *fr = &s->frame;
    int i, n = decide_frame_length(s);
    int aff = s->first->next->affinity;

    av_assert1(n < FF_ARRAY_ELEMS(fr->ifields));
    if (!n || fr->lock)
        return NULL;

    fr->lock++;
    fr->length = n;
    fr->parity = s->first->parity;
    fr->buffer = 0;

    for (i = 0; i < n; i++) {
        /* We cheat and steal the buffer without release+relock */
        fr->ifields[i]   = s->first->buffer;
        s->first->buffer = 0;
        s->first         = s->first->next;
    }

    if (n == 1) {
        fr->ofields[fr->parity    ] = fr->ifields[0];
        fr->ofields[fr->parity ^ 1] = 0;
    } else if (n == 2) {
        fr->ofields[fr->parity    ] = fr->ifields[0];
        fr->ofields[fr->parity ^ 1] = fr->ifields[1];
    } else if (n == 3) {
        if (!aff)
            aff = (fr->ifields[0] == fr->ifields[1]) ? -1 : 1;
        fr->ofields[fr->parity    ] = fr->ifields[1 + aff];
        fr->ofields[fr->parity ^ 1] = fr->ifields[1      ];
    }

    pullup_lock_buffer(fr->ofields[0], 0);
    pullup_lock_buffer(fr->ofields[1], 1);

    if (fr->ofields[0] == fr->ofields[1]) {
        fr->buffer = fr->ofields[0];
        pullup_lock_buffer(fr->buffer, 2);
        return fr;
    }

    return fr;
}

static void pullup_release_frame(PullupFrame *f)
{
    int i;

    for (i = 0; i < f->length; i++)
        pullup_release_buffer(f->ifields[i], f->parity ^ (i & 1));

    pullup_release_buffer(f->ofields[0], 0);
    pullup_release_buffer(f->ofields[1], 1);

    if (f->buffer)
        pullup_release_buffer(f->buffer, 2);
    f->lock--;
}

static void compute_metric(PullupContext *s, int *dest,
                           PullupField *fa, int pa, PullupField *fb, int pb,
                           int (*func)(const uint8_t *, const uint8_t *, int))
{
    int mp = s->metric_plane;
    int xstep = 8;
    int ystep = s->planewidth[mp] << 3;
    int stride = s->planewidth[mp] << 1; /* field stride */
    int w = s->metric_w * xstep;
    uint8_t *a, *b;
    int x, y;

    if (!fa->buffer || !fb->buffer)
        return;

    /* Shortcut for duplicate fields (e.g. from RFF flag) */
    if (fa->buffer == fb->buffer && pa == pb) {
        memset(dest, 0, s->metric_length * sizeof(*dest));
        return;
    }

    a = fa->buffer->planes[mp] + pa * s->planewidth[mp] + s->metric_offset;
    b = fb->buffer->planes[mp] + pb * s->planewidth[mp] + s->metric_offset;

    for (y = 0; y < s->metric_h; y++) {
        for (x = 0; x < w; x += xstep)
            *dest++ = func(a + x, b + x, stride);
        a += ystep; b += ystep;
    }
}

static int check_field_queue(PullupContext *s)
{
    int ret;

    if (s->head->next == s->first) {
        PullupField *f = av_mallocz(sizeof(*f));

        if (!f)
            return AVERROR(ENOMEM);

        if ((ret = alloc_metrics(s, f)) < 0) {
            av_free(f);
            return ret;
        }

        f->prev        = s->head;
        f->next        = s->first;
        s->head->next  = f;
        s->first->prev = f;
    }

    return 0;
}

static void pullup_submit_field(PullupContext *s, PullupBuffer *b, int parity)
{
    PullupField *f;

    /* Grow the circular list if needed */
    if (check_field_queue(s) < 0)
        return;

    /* Cannot have two fields of same parity in a row; drop the new one */
    if (s->last && s->last->parity == parity)
        return;

    f = s->head;
    f->parity   = parity;
    f->buffer   = pullup_lock_buffer(b, parity);
    f->flags    = 0;
    f->breaks   = 0;
    f->affinity = 0;

    compute_metric(s, f->diffs, f, parity, f->prev->prev, parity, s->diff);
    compute_metric(s, f->combs, parity ? f->prev : f, 0, parity ? f : f->prev, 1, s->comb);
    compute_metric(s, f->vars, f, parity, f, -1, s->var);
    emms_c();

    /* Advance the circular list */
    if (!s->first)
        s->first = s->head;

    s->last = s->head;
    s->head = s->head->next;
}

static void copy_field(PullupContext *s,
                       PullupBuffer *dst, PullupBuffer *src, int parity)
{
    uint8_t *dd, *ss;
    int i;

    for (i = 0; i < s->nb_planes; i++) {
        ss = src->planes[i] + parity * s->planewidth[i];
        dd = dst->planes[i] + parity * s->planewidth[i];

        av_image_copy_plane(dd, s->planewidth[i] << 1,
                            ss, s->planewidth[i] << 1,
                            s->planewidth[i], s->planeheight[i] >> 1);
    }
}

static void pullup_pack_frame(PullupContext *s, PullupFrame *fr)
{
    int i;

    if (fr->buffer)
        return;

    if (fr->length < 2)
        return; /* FIXME: deal with this */

    for (i = 0; i < 2; i++) {
        if (fr->ofields[i]->lock[i^1])
            continue;

        fr->buffer = fr->ofields[i];
        pullup_lock_buffer(fr->buffer, 2);
        copy_field(s, fr->buffer, fr->ofields[i^1], i^1);
        return;
    }

    fr->buffer = pullup_get_buffer(s, 2);

    copy_field(s, fr->buffer, fr->ofields[0], 0);
    copy_field(s, fr->buffer, fr->ofields[1], 1);
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    PullupContext *s = ctx->priv;
    PullupBuffer *b;
    PullupFrame *f;
    AVFrame *out;
    int p, ret = 0;

    b = pullup_get_buffer(s, 2);
    if (!b) {
        av_log(ctx, AV_LOG_WARNING, "Could not get buffer!\n");
        f = pullup_get_frame(s);
        pullup_release_frame(f);
        goto end;
    }

    av_image_copy(b->planes, s->planewidth,
                  (const uint8_t**)in->data, in->linesize,
                  inlink->format, inlink->w, inlink->h);

    p = in->interlaced_frame ? !in->top_field_first : 0;
    pullup_submit_field(s, b, p  );
    pullup_submit_field(s, b, p^1);

    if (in->repeat_pict)
        pullup_submit_field(s, b, p);

    pullup_release_buffer(b, 2);

    f = pullup_get_frame(s);
    if (!f)
        goto end;

    if (f->length < 2) {
        pullup_release_frame(f);
        f = pullup_get_frame(s);
        if (!f)
            goto end;
        if (f->length < 2) {
            pullup_release_frame(f);
            if (!in->repeat_pict)
                goto end;
            f = pullup_get_frame(s);
            if (!f)
                goto end;
            if (f->length < 2) {
                pullup_release_frame(f);
                goto end;
            }
        }
    }

    /* If the frame isn't already exportable... */
    if (!f->buffer)
        pullup_pack_frame(s, f);

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    av_frame_copy_props(out, in);

    av_image_copy(out->data, out->linesize,
                  (const uint8_t**)f->buffer->planes, s->planewidth,
                  inlink->format, inlink->w, inlink->h);

    ret = ff_filter_frame(outlink, out);
    pullup_release_frame(f);
end:
    av_frame_free(&in);
    return ret;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    PullupContext *s = ctx->priv;
    PullupField *f;
    int i;

    f = s->head;
    while (f) {
        av_free(f->diffs);
        av_free(f->combs);
        av_free(f->vars);
        if (f == s->last) {
            av_freep(&s->last);
            break;
        }
        f = f->next;
        av_freep(&f->prev);
    };

    for (i = 0; i < FF_ARRAY_ELEMS(s->buffers); i++) {
        av_freep(&s->buffers[i].planes[0]);
        av_freep(&s->buffers[i].planes[1]);
        av_freep(&s->buffers[i].planes[2]);
    }
}

static const AVFilterPad pullup_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
    { NULL }
};

static const AVFilterPad pullup_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_output,
    },
    { NULL }
};

AVFilter ff_vf_pullup = {
    .name          = "pullup",
    .description   = NULL_IF_CONFIG_SMALL("Pullup from field sequence to frames."),
    .priv_size     = sizeof(PullupContext),
    .priv_class    = &pullup_class,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = pullup_inputs,
    .outputs       = pullup_outputs,
};
