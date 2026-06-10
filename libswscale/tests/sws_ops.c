/*
 * Copyright (C) 2025 Niklas Haas
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

#include "libavutil/mem.h"
#include "libavutil/pixdesc.h"
#include "libswscale/ops.h"
#include "libswscale/ops_dispatch.h"
#include "libswscale/ops_internal.h"
#include "libswscale/format.h"

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

static int pass_idx;

static int print_ops(SwsContext *ctx, const SwsOpList *ops, SwsCompiledOp *out)
{
    if (pass_idx > 0)
        av_log(NULL, AV_LOG_INFO, " Sub-pass #%d:\n", pass_idx);

    ff_sws_op_list_print(NULL, AV_LOG_INFO, AV_LOG_INFO, ops);

    SwsUOpList *uops = ff_sws_uop_list_alloc();
    if (!uops)
        return AVERROR(ENOMEM);

    int ret = ff_sws_ops_translate(ctx, ops, 0, uops);
    if (ret == AVERROR(ENOTSUP)) {
        av_log(NULL, AV_LOG_INFO, " Retrying with split passes:\n");
        goto fail;
    } else if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error translating ops: %s\n", av_err2str(ret));
        goto fail;
    }

    av_log(NULL, AV_LOG_INFO, " translated micro-ops:\n");
    for (int i = 0; i < uops->num_ops; i++) {
        char name[SWS_UOP_NAME_MAX];
        ff_sws_uop_name(&uops->ops[i], name);
        av_log(NULL, AV_LOG_INFO, "    %s\n", name);
    }

    *out = (SwsCompiledOp) {0}; /* dummy value, will be immediately freed */
    pass_idx++;
    ret = 0;

fail:
    ff_sws_uop_list_free(&uops);
    return ret;
}

/* Dummy backend that just prints all seen op lists */
static const SwsOpBackend backend_print = {
    .name    = "print_ops",
    .compile = print_ops,
};

static int print_passes(SwsContext *ctx, void *graph, SwsOpList *ops)
{
    av_log(NULL, AV_LOG_INFO, "%s %dx%d -> %s %dx%d:\n",
           av_get_pix_fmt_name(ops->src.format),
           ops->src.width, ops->src.height,
           av_get_pix_fmt_name(ops->dst.format),
           ops->dst.width, ops->dst.height);

    if (ff_sws_op_list_is_noop(ops)) {
        av_log(NULL, AV_LOG_INFO, "  (no-op)\n");
        return 0;
    }

    /* ff_sws_compile_pass() takes over ownership of `ops` */
    SwsOpList *copy = ff_sws_op_list_duplicate(ops);
    if (!copy)
        return AVERROR(ENOMEM);

    pass_idx = 0;
    return ff_sws_compile_pass(graph, &backend_print, &copy, 0, NULL, NULL);
}
static void log_stdout(void *avcl, int level, const char *fmt, va_list vl)
{
    if (level != AV_LOG_INFO) {
        av_log_default_callback(avcl, level, fmt, vl);
    } else if (av_log_get_level() >= AV_LOG_INFO) {
        vfprintf(stdout, fmt, vl);
    }
}

int main(int argc, char **argv)
{
    enum AVPixelFormat src_fmt = AV_PIX_FMT_NONE;
    enum AVPixelFormat dst_fmt = AV_PIX_FMT_NONE;
    SwsContext *ctx = NULL;
    SwsGraph *graph = NULL;
    bool macros_gen = false;
    int ret = 1;

#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-help") || !strcmp(argv[i], "--help")) {
            fprintf(stderr,
                    "sws_ops [options...]\n"
                    "   -help\n"
                    "       This text\n"
                    "   -dst <pixfmt>\n"
                    "       Only test the specified destination pixel format\n"
                    "   -src <pixfmt>\n"
                    "       Only test the specified source pixel format\n"
                    "   -v <level>\n"
                    "       Enable log verbosity at given level\n"
                    "   -macros\n"
                    "       Generate helper macros\n"
            );
            return 0;
        }
        if (!strcmp(argv[i], "-src")) {
            if (i + 1 >= argc)
                goto bad_option;
            src_fmt = av_get_pix_fmt(argv[i + 1]);
            if (src_fmt == AV_PIX_FMT_NONE) {
                fprintf(stderr, "invalid pixel format %s\n", argv[i + 1]);
                return AVERROR(EINVAL);
            }
            i++;
        } else if (!strcmp(argv[i], "-dst")) {
            if (i + 1 >= argc)
                goto bad_option;
            dst_fmt = av_get_pix_fmt(argv[i + 1]);
            if (dst_fmt == AV_PIX_FMT_NONE) {
                fprintf(stderr, "invalid pixel format %s\n", argv[i + 1]);
                return AVERROR(EINVAL);
            }
            i++;
        } else if (!strcmp(argv[i], "-v")) {
            if (i + 1 >= argc)
                goto bad_option;
            av_log_set_level(atoi(argv[i + 1]));
            i++;
        } else if (!strcmp(argv[i], "-macros")) {
            macros_gen = true;
        } else {
bad_option:
            fprintf(stderr, "bad option or argument missing (%s) see -help\n", argv[i]);
            return AVERROR(EINVAL);
        }
    }

    if (macros_gen) {
        char *macros = NULL;
        ret = ff_sws_uops_macros_gen(&macros);
        if (ret >= 0)
            puts(macros);
        av_free(macros);
        return ret;
    }
    /* Allocate dummy graph and context for ff_sws_compile_pass() */
    graph = ff_sws_graph_alloc();
    if (!graph)
        goto fail;
    graph->ctx = ctx = sws_alloc_context();
    if (!ctx)
        goto fail;
    ctx->scaler = SWS_SCALE_BILINEAR; /* reduce filter generation overhead */

    av_log_set_callback(log_stdout);

    ret = ff_sws_enum_op_lists(ctx, graph, src_fmt, dst_fmt, print_passes);
    if (ret < 0)
        goto fail;

    ret = 0;
fail:
    sws_free_context(&ctx);
    ff_sws_graph_free(&graph);
    return ret;
}
