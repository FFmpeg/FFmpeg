/*
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
 * @file QR encoder source and filter.
 *
 * A QR code (quick-response code) is a type of two-dimensional matrix
 * barcode, invented in 1994, by Japanese company Denso Wave for
 * labelling automobile parts.
 *
 * This source uses the libqrencode library to generate QR code:
 * https://fukuchi.org/works/qrencode/
 */

//#define DEBUG

#include "config_components.h"

#include "libavutil/internal.h"
#include "libavutil/imgutils.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/lfg.h"
#include "libavutil/random_seed.h"

#include "avfilter.h"
#include "drawutils.h"
#include "filters.h"
#include "formats.h"
#include "textutils.h"
#include "video.h"
#include "libswscale/swscale.h"

#include <qrencode.h>

enum var_name {
    VAR_dar,
    VAR_duration,
    VAR_hsub, VAR_vsub,
    VAR_main_h, VAR_H,
    VAR_main_w, VAR_W,
    VAR_n,
    VAR_pict_type,
    VAR_qr_w, VAR_w,
    VAR_rendered_padded_qr_w, VAR_Q,
    VAR_rendered_qr_w, VAR_q,
    VAR_sar,
    VAR_t,
    VAR_x,
    VAR_y,
    VAR_VARS_NB
};

static const char *const var_names[] = {
    "dar",
    "duration",
    "hsub", "vsub",
    "main_h", "H",               ///< height of the input video
    "main_w", "W",               ///< width of the input video
    "n",                         ///< number of frame
    "pict_type",
    "qr_w", "w",                 ///< width of the QR code
    "rendered_padded_qr_w", "Q", ///< width of the rendered QR code
    "rendered_qr_w", "q",        ///< width of the rendered QR code
    "sar",
    "t",                         ///< timestamp expressed in seconds
    "x",
    "y",
    NULL
};

#define V(name_) qr->var_values[VAR_##name_]

enum Expansion {
    EXPANSION_NONE,
    EXPANSION_NORMAL
};

typedef struct QREncodeContext {
    const AVClass *class;

    char is_source;
    char *x_expr;
    char *y_expr;
    AVExpr *x_pexpr, *y_pexpr;

    char *rendered_qrcode_width_expr;
    char *rendered_padded_qrcode_width_expr;
    AVExpr *rendered_qrcode_width_pexpr, *rendered_padded_qrcode_width_pexpr;

    int rendered_qrcode_width;
    int rendered_padded_qrcode_width;

    unsigned char *text;
    char *textfile;
    uint64_t pts;

    int level;
    char case_sensitive;

    uint8_t foreground_color[4];
    uint8_t background_color[4];

    FFDrawContext draw;
    FFDrawColor draw_foreground_color;   ///< foreground color
    FFDrawColor draw_background_color;   ///< background color

    /* these are only used when nothing must be encoded */
    FFDrawContext draw0;
    FFDrawColor draw0_background_color;   ///< background color

    uint8_t *qrcode_data[4];
    int qrcode_linesize[4];
    uint8_t *qrcode_mask_data[4];
    int qrcode_mask_linesize[4];

    /* only used for filter to contain scaled image to blend on top of input */
    uint8_t *rendered_qrcode_data[4];
    int rendered_qrcode_linesize[4];

    int qrcode_width;
    int padded_qrcode_width;

    AVRational frame_rate;

    int expansion;                    ///< expansion mode to use for the text
    FFExpandTextContext expand_text;  ///< expand text in case expansion is enabled
    AVBPrint expanded_text;           ///< used to contain the expanded text

    double var_values[VAR_VARS_NB];
    AVLFG  lfg;                       ///< random generator
    AVDictionary *metadata;
} QREncodeContext;

#define OFFSET(x) offsetof(QREncodeContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
#define TFLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

#define COMMON_OPTIONS                                                  \
    { "qrcode_width", "set rendered QR code width expression", OFFSET(rendered_qrcode_width_expr), AV_OPT_TYPE_STRING, {.str = "64"}, 0, INT_MAX, FLAGS }, \
    { "q",            "set rendered QR code width expression", OFFSET(rendered_qrcode_width_expr), AV_OPT_TYPE_STRING, {.str = "64"}, 0, INT_MAX, FLAGS }, \
    { "padded_qrcode_width", "set rendered padded QR code width expression", OFFSET(rendered_padded_qrcode_width_expr), AV_OPT_TYPE_STRING, {.str = "q"}, 0, INT_MAX, FLAGS }, \
    { "Q",                   "set rendered padded QR code width expression", OFFSET(rendered_padded_qrcode_width_expr), AV_OPT_TYPE_STRING, {.str = "q"}, 0, INT_MAX, FLAGS }, \
    { "case_sensitive", "generate code which is case sensitive", OFFSET(case_sensitive), AV_OPT_TYPE_BOOL,   {.i64 = 1},      0,    1, FLAGS }, \
    { "cs",             "generate code which is case sensitive", OFFSET(case_sensitive), AV_OPT_TYPE_BOOL,   {.i64 = 1},      0,    1, FLAGS }, \
                                                                        \
    { "level", "error correction level, lowest is L", OFFSET(level), AV_OPT_TYPE_INT, { .i64 = AVCOL_SPC_UNSPECIFIED }, 0, QR_ECLEVEL_H, .flags = FLAGS, .unit = "level"}, \
    { "l",     "error correction level, lowest is L", OFFSET(level), AV_OPT_TYPE_INT, { .i64 = AVCOL_SPC_UNSPECIFIED }, 0, QR_ECLEVEL_H, .flags = FLAGS, .unit = "level"}, \
    { "L",     NULL, 0, AV_OPT_TYPE_CONST, { .i64 = QR_ECLEVEL_L }, 0, 0, FLAGS, .unit = "level" }, \
    { "M",     NULL, 0, AV_OPT_TYPE_CONST, { .i64 = QR_ECLEVEL_M }, 0, 0, FLAGS, .unit = "level" }, \
    { "Q",     NULL, 0, AV_OPT_TYPE_CONST, { .i64 = QR_ECLEVEL_Q }, 0, 0, FLAGS, .unit = "level" }, \
    { "H",     NULL, 0, AV_OPT_TYPE_CONST, { .i64 = QR_ECLEVEL_H }, 0, 0, FLAGS, .unit = "level" }, \
                                                                        \
    {"expansion", "set the expansion mode", OFFSET(expansion), AV_OPT_TYPE_INT, {.i64=EXPANSION_NORMAL}, 0, 2, FLAGS, .unit = "expansion"}, \
    {"none",     "set no expansion",     OFFSET(expansion), AV_OPT_TYPE_CONST, {.i64 = EXPANSION_NONE},     0, 0, FLAGS, .unit = "expansion"}, \
    {"normal",   "set normal expansion", OFFSET(expansion), AV_OPT_TYPE_CONST, {.i64 = EXPANSION_NORMAL},   0, 0, FLAGS, .unit = "expansion"}, \
                                                                        \
    { "foreground_color", "set QR foreground color", OFFSET(foreground_color), AV_OPT_TYPE_COLOR, {.str = "black"}, 0, 0, FLAGS }, \
    { "fc",               "set QR foreground color", OFFSET(foreground_color), AV_OPT_TYPE_COLOR, {.str = "black"}, 0, 0, FLAGS }, \
    { "background_color", "set QR background color", OFFSET(background_color), AV_OPT_TYPE_COLOR, {.str = "white"}, 0, 0, FLAGS }, \
    { "bc",               "set QR background color", OFFSET(background_color), AV_OPT_TYPE_COLOR, {.str = "white"}, 0, 0, FLAGS }, \
                                                                        \
    {"text",     "set text to encode", OFFSET(text), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, FLAGS}, \
    {"textfile", "set text file to encode", OFFSET(textfile), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, FLAGS}, \

static const char *const fun2_names[] = {
    "rand"
};

static double drand(void *opaque, double min, double max)
{
    return min + (max-min) / UINT_MAX * av_lfg_get(opaque);
}

static const ff_eval_func2 fun2[] = {
    drand,
    NULL
};

static int func_pts(void *ctx, AVBPrint *bp, const char *function_name,
                    unsigned argc, char **argv)
{
    QREncodeContext *qr = ((AVFilterContext *)ctx)->priv;
    const char *fmt;
    const char *strftime_fmt = NULL;
    const char *delta = NULL;
    double t = qr->var_values[VAR_t];

    // argv: pts, FMT, [DELTA, strftime_fmt]

    fmt = argc >= 1 ? argv[0] : "flt";
    if (argc >= 2) {
        delta = argv[1];
    }
    if (argc >= 3) {
        strftime_fmt = argv[2];
    }

    return ff_print_pts(ctx, bp, t, delta, fmt, strftime_fmt);
}

static int func_frame_num(void *ctx, AVBPrint *bp, const char *function_name,
                          unsigned argc, char **argv)
{
    QREncodeContext *qr = ((AVFilterContext *)ctx)->priv;

    av_bprintf(bp, "%d", (int)V(n));
    return 0;
}

static int func_strftime(void *ctx, AVBPrint *bp, const char *function_name,
                         unsigned argc, char **argv)
{
    const char *strftime_fmt = argc ? argv[0] : NULL;

    return ff_print_time(ctx, bp, strftime_fmt, !strcmp(function_name, "localtime"));
}

static int func_frame_metadata(void *ctx, AVBPrint *bp, const char *function_name,
                               unsigned argc, char **argv)
{
    QREncodeContext *qr = ((AVFilterContext *)ctx)->priv;
    AVDictionaryEntry *e = av_dict_get(qr->metadata, argv[0], NULL, 0);

    if (e && e->value)
        av_bprintf(bp, "%s", e->value);
    else if (argc >= 2)
        av_bprintf(bp, "%s", argv[1]);

    return 0;
}

static int func_eval_expr(void *ctx, AVBPrint *bp, const char *function_name,
                          unsigned argc, char **argv)
{
    QREncodeContext *qr = ((AVFilterContext *)ctx)->priv;

    return ff_print_eval_expr(ctx, bp, argv[0],
                              fun2_names, fun2,
                              var_names, qr->var_values, &qr->lfg);
}

static int func_eval_expr_formatted(void *ctx, AVBPrint *bp, const char *function_name,
                                    unsigned argc, char **argv)
{
    QREncodeContext *qr = ((AVFilterContext *)ctx)->priv;
    int ret;
    int positions = -1;

    /*
     * argv[0] expression to be converted to `int`
     * argv[1] format: 'x', 'X', 'd' or 'u'
     * argv[2] positions printed (optional)
     */

    if (argc == 3) {
        ret = sscanf(argv[2], "%u", &positions);
        if (ret != 1) {
            av_log(ctx, AV_LOG_ERROR, "expr_int_format(): Invalid number of positions"
                    " to print: '%s'\n", argv[2]);
            return AVERROR(EINVAL);
        }
    }

    return ff_print_formatted_eval_expr(ctx, bp, argv[0],
                                        fun2_names, fun2,
                                        var_names, qr->var_values,
                                        &qr->lfg,
                                        argv[1][0], positions);
}

static const FFExpandTextFunction expand_text_functions[] = {
    { "expr",            1, 1, func_eval_expr },
    { "e",               1, 1, func_eval_expr },
    { "expr_formatted",  2, 3, func_eval_expr_formatted },
    { "ef",              2, 3, func_eval_expr_formatted },
    { "metadata",        1, 2, func_frame_metadata },
    { "frame_num",       0, 0, func_frame_num },
    { "n",               0, 0, func_frame_num },
    { "gmtime",          0, 1, func_strftime },
    { "localtime",       0, 1, func_strftime },
    { "pts",             0, 3, func_pts }
};

static av_cold int init(AVFilterContext *ctx)
{
    QREncodeContext *qr = ctx->priv;
    int ret;

    av_lfg_init(&qr->lfg, av_get_random_seed());

    qr->qrcode_width = -1;
    qr->rendered_padded_qrcode_width = -1;

    if (qr->textfile) {
        if (qr->text) {
            av_log(ctx, AV_LOG_ERROR,
                   "Both text and text file provided. Please provide only one\n");
            return AVERROR(EINVAL);
        }
        if ((ret = ff_load_textfile(ctx, (const char *)qr->textfile, &(qr->text), NULL)) < 0)
            return ret;
    }

    qr->expand_text = (FFExpandTextContext) {
        .log_ctx = ctx,
        .functions = expand_text_functions,
        .functions_nb = FF_ARRAY_ELEMS(expand_text_functions)
    };

    av_bprint_init(&qr->expanded_text, 0, AV_BPRINT_SIZE_UNLIMITED);

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    QREncodeContext *qr = ctx->priv;

    av_expr_free(qr->x_pexpr);
    av_expr_free(qr->y_pexpr);

    av_bprint_finalize(&qr->expanded_text, NULL);

    av_freep(&qr->qrcode_data[0]);
    av_freep(&qr->rendered_qrcode_data[0]);
    av_freep(&qr->qrcode_mask_data[0]);
}

#ifdef DEBUG
static void show_qrcode(AVFilterContext *ctx, const QRcode *qrcode)
{
    int i, j;
    char *line = av_malloc(qrcode->width + 1);
    const char *p = qrcode->data;

    if (!line)
        return;
    for (i = 0; i < qrcode->width; i++) {
        for (j = 0; j < qrcode->width; j++)
            line[j] = (*p++)&1 ? '@' : ' ';
        line[j] = 0;
        av_log(ctx, AV_LOG_DEBUG, "%3d: %s\n", i, line);
    }
    av_free(line);
}
#endif

static int draw_qrcode(AVFilterContext *ctx, AVFrame *frame)
{
    QREncodeContext *qr = ctx->priv;
    struct SwsContext *sws = NULL;
    QRcode *qrcode = NULL;
    int i, j;
    char qrcode_width_changed;
    int ret;
    int offset;
    uint8_t *srcp;
    uint8_t *dstp0, *dstp;

    av_bprint_clear(&qr->expanded_text);

    switch (qr->expansion) {
    case EXPANSION_NONE:
        av_bprintf(&qr->expanded_text, "%s", qr->text);
        break;
    case EXPANSION_NORMAL:
        if ((ret = ff_expand_text(&qr->expand_text, qr->text, &qr->expanded_text)) < 0)
            return ret;
        break;
    }

    if (!qr->expanded_text.str || qr->expanded_text.str[0] == 0) {
        if (qr->is_source) {
            ff_fill_rectangle(&qr->draw0, &qr->draw0_background_color,
                              frame->data, frame->linesize,
                              0, 0, qr->rendered_padded_qrcode_width, qr->rendered_padded_qrcode_width);
        }

        return 0;
    }

    av_log(ctx, AV_LOG_DEBUG, "Encoding string '%s'\n", qr->expanded_text.str);
    qrcode = QRcode_encodeString(qr->expanded_text.str, 1, qr->level, QR_MODE_8,
                                 qr->case_sensitive);
    if (!qrcode) {
        ret = AVERROR(errno);
        av_log(ctx, AV_LOG_ERROR,
               "Failed to encode string with error \'%s\'\n", av_err2str(ret));
        goto end;
    }

    av_log(ctx, AV_LOG_DEBUG,
           "Encoded QR with width:%d version:%d\n", qrcode->width, qrcode->version);
#ifdef DEBUG
    show_qrcode(ctx, (const QRcode *)qrcode);
#endif

    qrcode_width_changed = qr->qrcode_width != qrcode->width;
    qr->qrcode_width = qrcode->width;

    // realloc mask if needed
    if (qrcode_width_changed) {
        av_freep(&qr->qrcode_mask_data[0]);
        ret = av_image_alloc(qr->qrcode_mask_data, qr->qrcode_mask_linesize,
                             qrcode->width, qrcode->width,
                             AV_PIX_FMT_GRAY8, 16);
        if (ret < 0) {
            av_log(ctx, AV_LOG_ERROR,
                   "Failed to allocate image for QR code with width %d\n", qrcode->width);
            goto end;
        }
    }

    /* fill mask */
    dstp0 = qr->qrcode_mask_data[0];
    srcp = qrcode->data;

    for (i = 0; i < qrcode->width; i++) {
        dstp = dstp0;
        for (j = 0; j < qrcode->width; j++)
            *dstp++ = (*srcp++ & 1) ? 255 : 0;
        dstp0 += qr->qrcode_mask_linesize[0];
    }

    if (qr->is_source) {
        if (qrcode_width_changed) {
            /* realloc padded image */

            // compute virtual non-rendered padded size
            // Q/q = W/w
            qr->padded_qrcode_width =
                ((double)qr->rendered_padded_qrcode_width / qr->rendered_qrcode_width) * qrcode->width;

            av_freep(&qr->qrcode_data[0]);
            ret = av_image_alloc(qr->qrcode_data, qr->qrcode_linesize,
                                 qr->padded_qrcode_width, qr->padded_qrcode_width,
                                 AV_PIX_FMT_ARGB, 16);
            if (ret < 0) {
                av_log(ctx, AV_LOG_ERROR,
                       "Failed to allocate image for QR code with width %d\n",
                       qr->padded_qrcode_width);
                goto end;
            }
        }

        /* fill padding */
        ff_fill_rectangle(&qr->draw, &qr->draw_background_color,
                          qr->qrcode_data, qr->qrcode_linesize,
                          0, 0, qr->padded_qrcode_width, qr->padded_qrcode_width);

        /* blend mask */
        offset = (qr->padded_qrcode_width - qr->qrcode_width) / 2;
        ff_blend_mask(&qr->draw, &qr->draw_foreground_color,
                      qr->qrcode_data, qr->qrcode_linesize,
                      qr->padded_qrcode_width, qr->padded_qrcode_width,
                      qr->qrcode_mask_data[0], qr->qrcode_mask_linesize[0], qrcode->width, qrcode->width,
                      3, 0, offset, offset);

        /* scale padded QR over the frame */
        sws = sws_alloc_context();
        if (!sws) {
            ret = AVERROR(ENOMEM);
            goto end;
        }

        av_opt_set_int(sws, "srcw", qr->padded_qrcode_width, 0);
        av_opt_set_int(sws, "srch", qr->padded_qrcode_width, 0);
        av_opt_set_int(sws, "src_format", AV_PIX_FMT_ARGB, 0);
        av_opt_set_int(sws, "dstw", qr->rendered_padded_qrcode_width, 0);
        av_opt_set_int(sws, "dsth", qr->rendered_padded_qrcode_width, 0);
        av_opt_set_int(sws, "dst_format", frame->format, 0);
        av_opt_set_int(sws, "sws_flags", SWS_POINT, 0);

        if ((ret = sws_init_context(sws, NULL, NULL)) < 0)
            goto end;

        sws_scale(sws,
                  (const uint8_t *const *)&qr->qrcode_data, qr->qrcode_linesize,
                  0, qr->padded_qrcode_width,
                  frame->data, frame->linesize);
    } else {
#define EVAL_EXPR(name_)                                        \
        av_expr_eval(qr->name_##_pexpr, qr->var_values, &qr->lfg);

        V(qr_w) = V(w) = qrcode->width;

        V(rendered_qr_w) = V(q) = EVAL_EXPR(rendered_qrcode_width);
        V(rendered_padded_qr_w) = V(Q) = EVAL_EXPR(rendered_padded_qrcode_width);
        /* It is necessary if q is expressed from Q */
        V(rendered_qr_w) = V(q) = EVAL_EXPR(rendered_qrcode_width);

        V(x) = EVAL_EXPR(x);
        V(y) = EVAL_EXPR(y);
        /* It is necessary if x is expressed from y */
        V(x) = EVAL_EXPR(x);

        av_log(ctx, AV_LOG_DEBUG,
               "Rendering QR code with values n:%d w:%d q:%d Q:%d x:%d y:%d t:%f\n",
               (int)V(n), (int)V(w), (int)V(q), (int)V(Q), (int)V(x), (int)V(y), V(t));

        /* blend rectangle over the target */
        ff_blend_rectangle(&qr->draw,  &qr->draw_background_color,
                           frame->data, frame->linesize, frame->width, frame->height,
                           V(x), V(y), V(Q), V(Q));

        if (V(q) != qr->rendered_qrcode_width) {
            av_freep(&qr->rendered_qrcode_data[0]);
            qr->rendered_qrcode_width = V(q);

            ret = av_image_alloc(qr->rendered_qrcode_data, qr->rendered_qrcode_linesize,
                                 qr->rendered_qrcode_width, qr->rendered_qrcode_width,
                                 AV_PIX_FMT_GRAY8, 16);
            if (ret < 0) {
                av_log(ctx, AV_LOG_ERROR,
                       "Failed to allocate image for rendered QR code with width %d\n",
                       qr->rendered_qrcode_width);
                goto end;
            }
        }

        /* scale mask */
        sws = sws_alloc_context();
        if (!sws) {
            ret = AVERROR(ENOMEM);
            goto end;
        }

        av_opt_set_int(sws, "srcw", qr->qrcode_width, 0);
        av_opt_set_int(sws, "srch", qr->qrcode_width, 0);
        av_opt_set_int(sws, "src_format", AV_PIX_FMT_GRAY8, 0);
        av_opt_set_int(sws, "dstw", qr->rendered_qrcode_width, 0);
        av_opt_set_int(sws, "dsth", qr->rendered_qrcode_width, 0);
        av_opt_set_int(sws, "dst_format", AV_PIX_FMT_GRAY8, 0);
        av_opt_set_int(sws, "sws_flags", SWS_POINT, 0);

        if ((ret = sws_init_context(sws, NULL, NULL)) < 0)
            goto end;

        sws_scale(sws,
                  (const uint8_t *const *)&qr->qrcode_mask_data, qr->qrcode_mask_linesize,
                  0, qr->qrcode_width,
                  qr->rendered_qrcode_data, qr->rendered_qrcode_linesize);

        /* blend mask over the input frame */
        offset = (V(Q) - V(q)) / 2;
        ff_blend_mask(&qr->draw, &qr->draw_foreground_color,
                      frame->data, frame->linesize, frame->width, frame->height,
                      qr->rendered_qrcode_data[0], qr->rendered_qrcode_linesize[0],
                      qr->rendered_qrcode_width, qr->rendered_qrcode_width,
                      3, 0, V(x) + offset, V(y) + offset);
    }

end:
    sws_freeContext(sws);
    QRcode_free(qrcode);

    return ret;
}

#if CONFIG_QRENCODESRC_FILTER

static const AVOption qrencodesrc_options[] = {
    COMMON_OPTIONS
    { "rate",     "set video rate", OFFSET(frame_rate), AV_OPT_TYPE_VIDEO_RATE, {.str = "25"}, 0, INT_MAX, FLAGS },
    { "r",        "set video rate", OFFSET(frame_rate), AV_OPT_TYPE_VIDEO_RATE, {.str = "25"}, 0, INT_MAX, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(qrencodesrc);

static int qrencodesrc_config_props(AVFilterLink *outlink)
{
    FilterLink *l = ff_filter_link(outlink);
    AVFilterContext *ctx = outlink->src;
    QREncodeContext *qr = ctx->priv;
    int ret;

    qr->is_source = 1;
    V(x) = V(y) = 0;

#define PARSE_AND_EVAL_EXPR(var_name_, expr_name_)                      \
    ret = av_expr_parse_and_eval(&qr->var_values[VAR_##var_name_],      \
                                 qr->expr_name_##_expr,                 \
                                 var_names, qr->var_values,             \
                                 NULL, NULL,                            \
                                 fun2_names, fun2,                      \
                                 &qr->lfg, 0, ctx);                     \
    if (ret < 0) {                                                      \
        av_log(ctx, AV_LOG_ERROR,                                       \
               "Could not evaluate expression '%s'\n",                  \
               qr->expr_name_##_expr);                                  \
        return ret;                                                     \
    }

    /* undefined for the source */
    V(main_w) = V(W) = NAN;
    V(main_h) = V(H) = NAN;
    V(x) = V(y) = V(t) = V(n) = NAN;
    V(dar) = V(sar) = 1.0;

    PARSE_AND_EVAL_EXPR(rendered_qr_w, rendered_qrcode_width);
    V(q) = V(rendered_qr_w);
    PARSE_AND_EVAL_EXPR(rendered_padded_qr_w, rendered_padded_qrcode_width);
    V(Q) = V(rendered_padded_qr_w);
    PARSE_AND_EVAL_EXPR(rendered_qr_w, rendered_qrcode_width);
    V(q) = V(rendered_qr_w);

    qr->rendered_qrcode_width = V(rendered_qr_w);
    qr->rendered_padded_qrcode_width = V(rendered_padded_qr_w);

    av_log(ctx, AV_LOG_VERBOSE,
           "q:%d Q:%d case_sensitive:%d level:%d\n",
           (int)qr->rendered_qrcode_width, (int)qr->rendered_padded_qrcode_width,
           qr->case_sensitive, qr->level);

    if (qr->rendered_padded_qrcode_width < qr->rendered_qrcode_width) {
        av_log(ctx, AV_LOG_ERROR,
               "Resulting padded QR code width (%d) is lesser than the QR code width (%d)\n",
               qr->rendered_padded_qrcode_width, qr->rendered_qrcode_width);
        return AVERROR(EINVAL);
    }

    ff_draw_init(&qr->draw, AV_PIX_FMT_ARGB, FF_DRAW_PROCESS_ALPHA);
    ff_draw_color(&qr->draw, &qr->draw_foreground_color, (const uint8_t *)&qr->foreground_color);
    ff_draw_color(&qr->draw, &qr->draw_background_color, (const uint8_t *)&qr->background_color);

    ff_draw_init2(&qr->draw0, outlink->format, outlink->colorspace, outlink->color_range, FF_DRAW_PROCESS_ALPHA);
    ff_draw_color(&qr->draw0, &qr->draw0_background_color, (const uint8_t *)&qr->background_color);

    outlink->w = qr->rendered_padded_qrcode_width;
    outlink->h = qr->rendered_padded_qrcode_width;
    outlink->time_base = av_inv_q(qr->frame_rate);
    l->frame_rate = qr->frame_rate;

    return 0;
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = (AVFilterContext *)outlink->src;
    QREncodeContext *qr = ctx->priv;
    AVFrame *frame =
        ff_get_video_buffer(outlink, qr->rendered_padded_qrcode_width, qr->rendered_padded_qrcode_width);
    int ret;

    if (!frame)
        return AVERROR(ENOMEM);
    frame->sample_aspect_ratio = (AVRational) {1, 1};
    V(n) = frame->pts = qr->pts++;
    V(t) = qr->pts * av_q2d(outlink->time_base);

    if ((ret = draw_qrcode(ctx, frame)) < 0)
        return ret;

    return ff_filter_frame(outlink, frame);
}

static int qrencodesrc_query_formats(AVFilterContext *ctx)
{
    enum AVPixelFormat pix_fmt;
    FFDrawContext draw;
    AVFilterFormats *fmts = NULL;
    int ret;

    // this is needed to support both the no-draw and draw cases
    // for the no-draw case we use FFDrawContext to write on the input picture ref
    for (pix_fmt = 0; av_pix_fmt_desc_get(pix_fmt); pix_fmt++)
        if (ff_draw_init(&draw, pix_fmt, 0) >= 0 &&
            sws_isSupportedOutput(pix_fmt) &&
            (ret = ff_add_format(&fmts, pix_fmt)) < 0)
            return ret;

    return ff_set_common_formats(ctx, fmts);
}

static const AVFilterPad qrencodesrc_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = request_frame,
        .config_props  = qrencodesrc_config_props,
    }
};

const AVFilter ff_vsrc_qrencodesrc = {
    .name          = "qrencodesrc",
    .description   = NULL_IF_CONFIG_SMALL("Generate a QR code."),
    .priv_size     = sizeof(QREncodeContext),
    .priv_class    = &qrencodesrc_class,
    .init          = init,
    .uninit        = uninit,
    .inputs        = NULL,
    FILTER_OUTPUTS(qrencodesrc_outputs),
    FILTER_QUERY_FUNC(qrencodesrc_query_formats),
};

#endif // CONFIG_QRENCODESRC_FILTER

#if CONFIG_QRENCODE_FILTER

static const AVOption qrencode_options[] = {
    COMMON_OPTIONS
    {"x",              "set x expression",      OFFSET(x_expr),             AV_OPT_TYPE_STRING, {.str="0"},   0, 0, TFLAGS},
    {"y",              "set y expression",      OFFSET(y_expr),             AV_OPT_TYPE_STRING, {.str="0"},   0, 0, TFLAGS},
    { NULL }
};

AVFILTER_DEFINE_CLASS(qrencode);

static int qrencode_config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    QREncodeContext *qr = ctx->priv;
    char *expr;
    int ret;

    qr->is_source = 0;

    ff_draw_init2(&qr->draw, inlink->format, inlink->colorspace, inlink->color_range,
                  FF_DRAW_PROCESS_ALPHA);

    V(W) = V(main_w) = inlink->w;
    V(H) = V(main_h) = inlink->h;
    V(sar)  = inlink->sample_aspect_ratio.num ? av_q2d(inlink->sample_aspect_ratio) : 1;
    V(dar)  = (double)inlink->w / inlink->h * V(sar);
    V(hsub) = 1 << qr->draw.hsub_max;
    V(vsub) = 1 << qr->draw.vsub_max;
    V(t) = NAN;
    V(x) = V(y) = NAN;

    qr->x_pexpr = qr->y_pexpr = NULL;
    qr->x_pexpr = qr->y_pexpr = NULL;

#define PARSE_EXPR(name_)                                               \
    ret = av_expr_parse(&qr->name_##_pexpr, expr = qr->name_##_expr, var_names, \
                        NULL, NULL, fun2_names, fun2, 0, ctx);          \
    if (ret < 0) {                                                      \
        av_log(ctx, AV_LOG_ERROR,                                       \
               "Could not to parse expression '%s' for '%s'\n",         \
               expr, #name_);                                           \
        return AVERROR(EINVAL);                                         \
    }

    PARSE_EXPR(x);
    PARSE_EXPR(y);
    PARSE_EXPR(rendered_qrcode_width);
    PARSE_EXPR(rendered_padded_qrcode_width);

    ff_draw_init2(&qr->draw, inlink->format, inlink->colorspace, inlink->color_range,
                  FF_DRAW_PROCESS_ALPHA);
    ff_draw_color(&qr->draw, &qr->draw_foreground_color, (const uint8_t *)&qr->foreground_color);
    ff_draw_color(&qr->draw, &qr->draw_background_color, (const uint8_t *)&qr->background_color);

    qr->rendered_qrcode_width = -1;

    return 0;
}

static int qrencode_query_formats(AVFilterContext *ctx)
{
    return ff_set_common_formats(ctx, ff_draw_supported_pixel_formats(0));
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    FilterLink *inl = ff_filter_link(inlink);
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    QREncodeContext *qr = ctx->priv;
    int ret;

    V(n) = inl->frame_count_out;
    V(t) = frame->pts == AV_NOPTS_VALUE ?
        NAN : frame->pts * av_q2d(inlink->time_base);
    V(pict_type) = frame->pict_type;
    V(duration) = frame->duration * av_q2d(inlink->time_base);

    qr->metadata = frame->metadata;

    if ((ret = draw_qrcode(ctx, frame)) < 0)
        return ret;

    return ff_filter_frame(outlink, frame);
}

static const AVFilterPad avfilter_vf_qrencode_inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_VIDEO,
        .flags          = AVFILTERPAD_FLAG_NEEDS_WRITABLE,
        .filter_frame   = filter_frame,
        .config_props   = qrencode_config_input,
    },
};

const AVFilter ff_vf_qrencode = {
    .name          = "qrencode",
    .description   = NULL_IF_CONFIG_SMALL("Draw a QR code on top of video frames."),
    .priv_size     = sizeof(QREncodeContext),
    .priv_class    = &qrencode_class,
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(avfilter_vf_qrencode_inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
    FILTER_QUERY_FUNC(qrencode_query_formats),
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};

#endif // CONFIG_QRENCODE_FILTER
